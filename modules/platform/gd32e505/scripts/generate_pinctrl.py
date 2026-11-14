#!/usr/bin/env python3
"""从封装配置生成 E505 引脚定义，保留硬件重映射编号。"""

from pathlib import Path
import argparse
import re
import yaml

ROOT = Path(__file__).resolve().parents[1]
MODES = {"inp": "GPIO_IN", "out": "ALTERNATE", "analog": "ANALOG"}


def generate(package, memory=None):
    config = yaml.safe_load((ROOT / "pinconfigs/gd32e505xx.yml").read_text())
    rows = [
        "/* SPDX-License-Identifier: Apache-2.0 */",
        "/* 由 scripts/generate_pinctrl.py 生成，请修改 pinconfigs 配置源。 */",
        "#include <dt-bindings/pinctrl/gd32e505xx-afio.h>",
        "",
    ]
    for signal, settings in {"ANALOG": {"modes": ["analog"]}, **config["signal-configs"]}.items():
        if package in settings.get("exclude-pincodes", []):
            continue
        # 数据手册表 2-3～2-5 注(4)：这些定时器仅 E 型号提供。
        if memory == "C" and re.match(r"TIMER(?:7|8|9|10|11|12|13)_", signal):
            continue
        remaps = config["remaps"].get(signal, {}).get("pins", [])
        for pin, entry in config["pins"].items():
            if package not in entry["pincodes"]:
                continue
            options = [i for i, p in enumerate(remaps) if p == pin]
            if not remaps and (signal in entry["afs"] or signal == "ANALOG"):
                options = [None]
            for option in options:
                suffix = ""
                remap = "NORMP"
                if option is not None:
                    if len(remaps) == 2:
                        suffix = ["NORMP", "RMP"][option]
                    elif len(remaps) == 4 and signal.startswith("TIMER1_"):
                        suffix = ["NORMP", "PRMP1", "PRMP2", "FRMP"][option]
                    else:
                        suffix = {
                            0: "NORMP",
                            1: "PRMP",
                            2: "PRMP" if signal.startswith("TIMER2_") else "FRMP",
                            3: "FRMP",
                        }[option]
                    group = signal.split("_")[0]
                    remap = ("SPI2" if group == "I2S2" else group) + "_" + suffix
                selector = entry.get("selectors", {}).get(signal, 0)
                for mode in settings["modes"]:
                    name = signal + "_" + pin
                    if len(settings["modes"]) > 1:
                        name += "_" + mode.upper()
                    if suffix:
                        name += "_" + suffix
                    expression = f"GD32_PINMUX_AFIO('{pin[1]}', {pin[2:]}, {MODES[mode]}, {remap})"
                    if selector:
                        expression = f"({expression} | ({selector}U << 20U))"
                    rows.extend([f"#define {name} \\", "    " + expression])
    return "\n".join(rows) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="Check generated headers")
    args = parser.parse_args()
    for package in "ZVR":
        for memory in (None, "E", "C"):
            code = "(e-c)" if memory is None else memory.lower()
            path = ROOT / f"include/dt-bindings/pinctrl/gd32e505{package.lower()}{code}xx-pinctrl.h"
            content = generate(package, memory)
            if args.check:
                assert path.read_text() == content, f"Stale header: {path}"
            else:
                path.write_text(content)


if __name__ == "__main__":
    main()
