#!/usr/bin/env python3
"""生成可迁移的离线构建包；依赖快照只读，不携带 Git 历史或签名私钥。"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

APP = Path(__file__).resolve().parents[1]
WORKSPACE = APP.parents[1]
REPOS = {
    "zephyr": "zephyr",
    "cmsis_6": "modules/hal/cmsis_6",
    "hal_gigadevice": "modules/hal/gigadevice",
    "mcuboot": "bootloader/mcuboot",
}


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args])


def copy_sources(source, target, product=False):
    paths = git(source, "ls-files", "-z").decode().split("\0")
    if product:
        paths += ["scripts/package-offline.py", "scripts/offline-build.sh"]
    for name in set(paths):
        if not name:
            continue
        relative = Path(name)
        if any(part in {".git", ".scratch", "__pycache__"} for part in relative.parts):
            continue
        if relative.suffix in {".pem", ".key"}:
            continue
        if not product and relative.parts[0] in {"doc", "tests", "samples", ".github"}:
            continue
        original = source / relative
        if not original.is_file():
            continue
        destination = target / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, destination, follow_symlinks=False)


def main():
    parser = argparse.ArgumentParser(description="Package the IDU-pre offline build workspace")
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--wheels", type=Path, required=True)
    parser.add_argument(
        "--output", type=Path, default=WORKSPACE / "IDU-pre-offline-linux-x86_64.tar.gz"
    )
    args = parser.parse_args()
    stage = WORKSPACE / "build/offline-package/IDU-pre-offline"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    versions = {}
    projects = []
    for name, path in REPOS.items():
        source = WORKSPACE / path
        revision = git(source, "rev-parse", "HEAD").decode().strip()
        versions[name] = revision
        copy_sources(source, stage / path)
        projects.append(
            {
                "name": name,
                "path": path,
                "revision": revision,
                "url": git(
                    source, "remote", "get-url", git(source, "remote").decode().splitlines()[0]
                )
                .decode()
                .strip(),
                **({"west-commands": "scripts/west-commands.yml"} if name == "zephyr" else {}),
            }
        )
    copy_sources(APP, stage / "applications/IDU-pre", product=True)
    versions["IDU-pre"] = git(APP, "rev-parse", "HEAD").decode().strip()
    versions["product_worktree_changes"] = git(APP, "status", "--short").decode()
    versions["sdk"] = (args.sdk / "sdk_version").read_text().strip()
    (stage / "manifest").mkdir()
    # JSON 是合法 YAML；展开后的清单不需要 Git 元数据或远程 import。
    (stage / "manifest/west.yml").write_text(
        json.dumps({"manifest": {"projects": projects, "self": {"path": "manifest"}}}, indent=2)
    )
    (stage / ".west").mkdir()
    (stage / ".west/config").write_text("[manifest]\npath = manifest\nfile = west.yml\n")
    sdk = stage / "toolchain"
    sdk.mkdir()
    for name in ("cmake", "sdk_version", "sdk_gnu_toolchains"):
        original = args.sdk / name
        if original.is_dir():
            shutil.copytree(original, sdk / name, symlinks=True)
        else:
            shutil.copy2(original, sdk / name)
    shutil.copytree(args.sdk / "gnu/arm-zephyr-eabi", sdk / "gnu/arm-zephyr-eabi", symlinks=True)
    shutil.copytree(args.wheels, stage / "wheels")
    (stage / "requirements.txt").write_text(
        "west\npyelftools\nPyYAML\npykwalify\npackaging\nintelhex\n"
        "cryptography\nclick\ncbor2\nsetuptools\ncmake\nninja\njsonschema\njinja2\n"
    )
    shutil.copy2(APP / "scripts/offline-build.sh", stage / "build.sh")
    (stage / "build.sh").chmod(0o755)
    (stage / "VERSIONS.json").write_text(json.dumps(versions, indent=2))
    (stage / "README.md").write_text(
        "# IDU-pre 离线编译包\n\n"
        "适用 Linux x86_64 / WSL2，Python >= 3.12，glibc >= 2.35。\n"
        "原生 wheel 已提供 Python 3.12/3.13/3.14 版本，其他版本可能需补充 wheel。\n"
        "不适用 Windows PowerShell 原生环境或 ARM Mac。\n\n"
        "新 Ubuntu 首次准备（需要联网，仅安装系统前置依赖）：\n\n"
        "```bash\nsudo apt update\n"
        "sudo apt install python3 python3-venv git device-tree-compiler\n```\n\n"
        "解压后在此目录执行 `./build.sh --pristine always`；构建本身不联网。\n"
        "脚本创建 .venv，使用包内 wheel、CMake/Ninja 和 ARM GCC。\n"
        "不需要 west init/update，也不需要全局安装 SDK。\n\n"
        "输出：build/hex/IDU-pre-mcuboot-iapv3/。\n"
        "首次构建生成新的开发密钥，仅适合完整工厂烧录。升级旧设备时请使用\n"
        "`./build.sh --key /path/to/original.pem`，并单独安全迁移原私钥。\n"
        "本包不含任何现有私钥、Git 历史、旧构建结果、烧录器或仿真器。\n"
        "依赖只裁去文档/测试/示例；保留许可证。版本见 VERSIONS.json。\n"
        "校验包内文件：`sha256sum -c SHA256SUMS`。\n"
    )
    with (stage / "SHA256SUMS").open("w") as hashes:
        for path in sorted(stage.rglob("*")):
            if path.is_file() and not path.is_symlink() and path.name != "SHA256SUMS":
                digest = hashlib.file_digest(path.open("rb"), "sha256").hexdigest()
                hashes.write(f"{digest}  {path.relative_to(stage)}\n")
    with tarfile.open(args.output, "w:gz", compresslevel=6) as archive:
        archive.add(stage, arcname=stage.name)
    digest = hashlib.file_digest(args.output.open("rb"), "sha256").hexdigest()
    args.output.with_suffix(args.output.suffix + ".sha256").write_text(
        f"{digest}  {args.output.name}\n"
    )
    print(f"Created {args.output} ({args.output.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
