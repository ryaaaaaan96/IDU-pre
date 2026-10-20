"""Exercise product Sysbuild defaults and application feature requirements."""
from pathlib import Path
import subprocess
import tempfile
import unittest

APP = Path(__file__).resolve().parents[1]


class SysbuildDefaults(unittest.TestCase):
    def configure(self, assignments, expected):
        script = '''cmake_minimum_required(VERSION 3.20.0)
function(find_package)
endfunction()
function(project)
endfunction()
'''
        for name, value in assignments.items():
            script += f'set({name} [=[{value}]=] CACHE STRING "test input")\n'
        script += f'include([=[{APP / "bootloader/CMakeLists.txt"}]=])\n'
        for name, value in expected.items():
            script += f'''if(NOT "${{{name}}}" STREQUAL [=[{value}]=])
  message(FATAL_ERROR "Unexpected {name}: '${{{name}}}'")
endif()
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'check.cmake'
            source.write_text(script)
            result = subprocess.run(['cmake', '-P', str(source)], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_default_image_paths(self):
        self.configure({}, {
            'mcuboot_EXTRA_DTC_OVERLAY_FILE': str(APP / 'bootloader/boot.overlay'),
            'IDU-pre_EXTRA_DTC_OVERLAY_FILE': str(APP / 'bootloader/app.overlay'),
            'mcuboot_EXTRA_CONF_FILE': str(APP / 'bootloader/mcuboot.conf'),
            'IDU-pre_EXTRA_CONF_FILE': str(APP / 'bootloader/app.conf'),
            'ZEPHYR_EXTRA_MODULES': str(APP / 'modules/gd32e505'),
        })

    def test_custom_image_paths_and_multiple_overlays_survive(self):
        values = {
            'mcuboot_EXTRA_DTC_OVERLAY_FILE': '/custom/layout.overlay;/custom/uart.overlay',
            'IDU-pre_EXTRA_DTC_OVERLAY_FILE': '/custom/application.overlay',
            'mcuboot_EXTRA_CONF_FILE': '/custom/boot.conf',
            'IDU-pre_EXTRA_CONF_FILE': '/custom/app.conf',
        }
        self.configure(values, values)

    def test_explicit_empty_override_survives(self):
        self.configure({'mcuboot_EXTRA_DTC_OVERLAY_FILE': ''},
                       {'mcuboot_EXTRA_DTC_OVERLAY_FILE': ''})

    def test_previous_default_paths_migrate(self):
        self.configure({
            'mcuboot_EXTRA_DTC_OVERLAY_FILE': str(APP / 'boards/mcuboot-boot.overlay'),
            'IDU-pre_EXTRA_DTC_OVERLAY_FILE': str(APP / 'boards/mcuboot.overlay'),
            'mcuboot_EXTRA_CONF_FILE': str(APP / 'sysbuild/mcuboot.conf'),
            'IDU-pre_EXTRA_CONF_FILE': str(APP / 'conf/mcuboot.conf'),
        }, {
            'mcuboot_EXTRA_DTC_OVERLAY_FILE': str(APP / 'bootloader/boot.overlay'),
            'IDU-pre_EXTRA_DTC_OVERLAY_FILE': str(APP / 'bootloader/app.overlay'),
            'mcuboot_EXTRA_CONF_FILE': str(APP / 'bootloader/mcuboot.conf'),
            'IDU-pre_EXTRA_CONF_FILE': str(APP / 'bootloader/app.conf'),
        })

    def test_user_modules_are_preserved_without_duplicate_hardware_module(self):
        modules = f'/custom/module;{APP / "modules/gd32e505"}'
        self.configure({'ZEPHYR_EXTRA_MODULES': modules}, {'ZEPHYR_EXTRA_MODULES': modules})


class ApplicationRequirements(unittest.TestCase):
    required = (
        'DATAPROCESS', 'DP_PROCESS', 'DP_REFERENCE_TABLES', 'DP_PARAMS', 'COLLECT',
        'TRANSMIT', 'TR_MODBUS_ADAPTER', 'TR_MODBUS_TRANSPORT', 'TR_SERVER_SERVICE',
        'TR_REGISTER_SERVICE', 'GPIO', 'ADC', 'ANALOG_INPUT', 'DIGITAL_IO',
        'IDU_INPUTS', 'LED', 'WATCHDOG_MONITOR',
    )

    def configure(self, **overrides):
        values = {name: 'y' for name in self.required}
        values.update(IDU_COLLECT_VIRTUAL='y', CO_SIMULATOR='y', TR_MVIO='y',
                      IDU_AIDC_PROFILE='n', CO_MODEL_SIMULATOR='n', DP_MAX_BUSES='1')
        values.update(overrides)
        script = 'cmake_minimum_required(VERSION 3.20.0)\n'
        # Execute the actual configure checks without compiling a firmware image.
        for command in ('target_include_directories', 'target_sources', 'add_subdirectory'):
            script += f'function({command})\nendfunction()\n'
        for name, value in values.items():
            # CMake uses ON/OFF, while generated Zephyr y/n become defined/undefined.
            value = {'y': 'ON', 'n': 'OFF'}.get(value, value)
            script += f'set(CONFIG_{name} {value})\n'
        script += f'include([=[{APP / "app/CMakeLists.txt"}]=])\n'
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'check.cmake'
            source.write_text(script)
            return subprocess.run(['cmake', '-P', str(source)], text=True, capture_output=True)

    def test_required_services_fail_during_configuration(self):
        for name in self.required:
            with self.subTest(feature=name):
                result = self.configure(**{name: 'n'})
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f'requires CONFIG_{name}=y', result.stderr)

    def test_default_and_rtu_profiles(self):
        for overrides in ({}, {'IDU_COLLECT_VIRTUAL': 'n', 'CO_SIMULATOR': 'n'}):
            result = self.configure(**overrides)
            self.assertEqual(result.returncode, 0, result.stderr)
        for feature in ('CO_SIMULATOR', 'TR_MVIO'):
            result = self.configure(**{feature: 'n'})
            self.assertIn(f'requires CONFIG_{feature}=y', result.stderr)

    def test_aidc_requires_model_backend_and_second_bus(self):
        result = self.configure(IDU_AIDC_PROFILE='y', CO_MODEL_SIMULATOR='y',
                                TR_MVIO='n', DP_MAX_BUSES='2')
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.configure(IDU_AIDC_PROFILE='y', DP_MAX_BUSES='2')
        self.assertIn('requires CONFIG_CO_MODEL_SIMULATOR=y', result.stderr)
        result = self.configure(IDU_AIDC_PROFILE='y', CO_MODEL_SIMULATOR='y')
        self.assertIn('requires CONFIG_DP_MAX_BUSES >= 2', result.stderr)


if __name__ == '__main__':
    unittest.main()
