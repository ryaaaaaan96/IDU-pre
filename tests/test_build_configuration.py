"""验证产品 Sysbuild 默认值及应用功能依赖。"""
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

    def test_user_modules_are_preserved_without_duplicate_hardware_module(self):
        modules = f'/custom/module;{APP / "modules/gd32e505"}'
        self.configure({'ZEPHYR_EXTRA_MODULES': modules}, {'ZEPHYR_EXTRA_MODULES': modules})


class ApplicationRequirements(unittest.TestCase):
    required = (
        'BOOTLOADER_MCUBOOT',
        'DATAPROCESS', 'GPIO', 'ADC',
    )

    def configure(self, **overrides):
        values = {name: 'y' for name in self.required}
        values.update(overrides)
        script = 'cmake_minimum_required(VERSION 3.20.0)\n'
        # 运行真实配置检查，不编译固件镜像。
        for command in ('target_include_directories', 'target_sources', 'add_subdirectory'):
            script += f'function({command})\nendfunction()\n'
        for name, value in values.items():
            # CMake 使用 ON/OFF，而生成的 Zephyr y/n 对应已定义或未定义。
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



if __name__ == '__main__':
    unittest.main()
