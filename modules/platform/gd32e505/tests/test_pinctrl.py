"""对所有封装的宏执行 C 编译，核对保留编码和 Alternate1 选择。"""

import importlib.util
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
HAL = ROOT.parent / "hal_gigadevice"
spec = importlib.util.spec_from_file_location("generator", ROOT / "scripts/generate_pinctrl.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class PinctrlTest(unittest.TestCase):
    def test_all_macros_compile(self):
        for header in (ROOT / "include/dt-bindings/pinctrl").glob("*pinctrl.h"):
            names = re.findall(r"^#define (\w+)", header.read_text(), re.M)
            source = f"#include <dt-bindings/pinctrl/{header.name}>\n"
            source += "unsigned values[] = {" + ",".join(names) + "};\n"
            subprocess.run(
                [
                    "cc",
                    "-x",
                    "c",
                    "-fsyntax-only",
                    "-I",
                    str(ROOT / "include"),
                    "-I",
                    str(HAL / "include"),
                    "-",
                ],
                input=source,
                text=True,
                check=True,
            )

    def test_routes(self):
        content = generator.generate("Z", "E")
        self.assertIn("GD32_PINMUX_AFIO('E', 15, GPIO_IN, TIMER0_FRMP)", content)
        self.assertIn("GD32_PINMUX_AFIO('B', 4, GPIO_IN, TIMER2_PRMP)", content)
        self.assertIn("GD32_PINMUX_AFIO('A', 9, GPIO_IN, NORMP) | (1U << 20U)", content)
        self.assertIn("USBHS_ULPI_STP_PC0", content)
        self.assertIn("SHRTIMER_EXEV9_PG13", content)
        self.assertNotIn("TIMER7_", generator.generate("V", "C"))
        self.assertNotIn("I2S2_RMP)", content)


if __name__ == "__main__":
    unittest.main()
