"""Read-only KLQ environment check; never connects to or flashes a board."""
import argparse
import hashlib
from importlib import metadata
import json
import os
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=ROOT, help="Source checkout to inspect")
    p.add_argument("--armcc", type=Path, default=Path(os.getenv("KLQ_ARMCC", "C:/Keil_v5/ARM/ARMCC/bin")))
    p.add_argument("--require-runtime", action="store_true")
    p.add_argument("--ul", action="store_true", help="Also check ultrasonic sensor dependencies")
    p.add_argument("--voice", action="store_true", help="Also check CI1303 dependencies")
    p.add_argument("--flash", action="store_true", help="Check KLQ SWD pack without contacting a probe")
    p.add_argument("--pack", type=Path, default=Path(os.getenv("LOCALAPPDATA", "")) / "Arm/Packs/Geehy/APM32E1xx_DFP/1.0.3")
    p.add_argument("--strict", action="store_true", help="Treat warnings as failures")
    args = p.parse_args()
    root = args.root.resolve()
    pins = json.loads((ROOT / "tools/environment_baseline.json").read_text(encoding="utf-8"))
    failures, warnings = [], []

    def report(level, message):
        print(f"[{level}] {message}")
        if level == "FAIL":
            failures.append(message)
        elif level == "WARN":
            warnings.append(message)

    def require(path):
        if not path.is_file():
            report("FAIL", f"Missing file: {path}")
            return False
        return True

    def run(argv, cwd=None):
        try:
            r = subprocess.run([str(a) for a in argv], cwd=cwd or root, capture_output=True,
                               text=True, encoding="utf-8", errors="replace", timeout=30)
        except (OSError, subprocess.TimeoutExpired) as exc:
            report("FAIL", f"{argv[0]}: {exc}")
            return None
        if r.returncode:
            report("FAIL", f"{argv[0]} exited {r.returncode}: {(r.stderr or r.stdout).strip()[:300]}")
            return None
        return (r.stdout + r.stderr).strip()

    def version(label, argv, expected):
        result = run(argv)
        if result is not None:
            report("OK" if expected in result else "FAIL",
                   f"{label}: expected {expected}; {result.splitlines()[0]}")

    def checksum(path, expected):
        if require(path):
            with path.open("rb") as f:
                actual = hashlib.file_digest(f, "sha256").hexdigest()
            report("OK" if actual == expected else "FAIL", f"SHA256 {path.name}: {actual}")

    def tracked(rel):
        result = run(["git", "ls-files", "--", rel])
        if result == "":
            report("WARN", f"Present locally but not tracked in this checkout: {rel}")

    report("OK" if platform.python_version() == pins["python"] else "WARN",
           f"Python {platform.python_version()}; baseline {pins['python']}")
    if platform.system() != "Windows":
        report("FAIL", "This baseline uses Windows .exe toolchains.")
    for exe in ("armcc.exe", "armasm.exe", "armlink.exe", "fromelf.exe"):
        if require(args.armcc / exe):
            version(exe, [args.armcc / exe, "--vsn"], pins["armcc"])
    package_count = 0
    for line in (ROOT / "tools/requirements.lock.txt").read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        name, expected = line.split("==", 1)
        try:
            actual = metadata.version(name)
        except metadata.PackageNotFoundError:
            report("FAIL", f"Missing Python package: {name}=={expected}")
            continue
        if actual != expected:
            report("FAIL", f"Python package {name}: {actual}, expected {expected}")
        package_count += 1
    report("INFO", f"Checked {package_count} installed Python packages against lock file.")
    for rel in ("tools/build.py", "bootloader/startup.s",
                "vendor/APM32E10x_EVAL_SDK-main/KLQ_SOURCE.json",
                "vendor/APM32E10x_EVAL_SDK-main/Libraries/CMSIS/Include/core_cm3.h",
                "vendor/APM32E10x_EVAL_SDK-main/Libraries/USB_Device_Lib/Core_Device/Standard/src/usbd_interrupt.c"):
        require(root / rel)
    ref = run(["git", "log", "-1", "--format=%h %s"])
    if ref:
        report("INFO", f"Checkout: {root}; HEAD {ref}")
    if args.require_runtime:
        for name in ("mini_python.c", "klq_runtime.c", "power_key.c"):
            rel = f"bootloader/{name}"
            if require(root / rel):
                tracked(rel)
    elif not (root / "bootloader/mini_python.c").is_file():
        report("WARN", "Python runtime source is absent; check the intended Git branch.")

    if args.flash:
        if args.pack.is_file():
            checksum(args.pack, pins["geehy_pack"]["sha256"])
        else:
            pdsc = args.pack / "Geehy.APM32E1xx_DFP.pdsc"
            if require(pdsc):
                import xml.etree.ElementTree as ET
                try:
                    release = ET.parse(pdsc).find("./releases/release")
                    actual = release.get("version") if release is not None else None
                    report("OK" if actual == pins["geehy_pack"]["version"] else "FAIL",
                           f"Geehy installed pack: {actual}")
                except ET.ParseError as exc:
                    report("FAIL", f"Invalid Geehy pack descriptor: {exc}")
            require(args.pack / "Flash/APM32E10x_512.FLM")
            report("INFO", "Installed pack checked by descriptor; archive SHA256 requires --pack file.pack.")

    if args.ul:
        for rel in ("UL/tools/build_sensor.ps1", "UL/firmware/app/Src/main.c",
                    "UL/firmware/linker/application.sct"):
            if require(root / rel):
                tracked(rel)
        sdk = root / pins["ul_sdk"]["path"]
        if (sdk / ".git").exists():
            commit = run(["git", "rev-parse", "HEAD"], sdk)
            if commit is not None:
                report("OK" if commit == pins["ul_sdk"]["commit"] else "FAIL", f"Puya SDK commit: {commit}")
            changes = run(["git", "status", "--porcelain"], sdk)
            if changes:
                report("WARN", "Puya SDK has local changes; the commit alone will not reproduce them.")
        else:
            report("WARN", "Puya SDK has no Git metadata; verify the imported SDK against the pinned commit.")
        require(sdk / "Drivers/PY32F002B_HAL_Driver/Src/py32f002b_ll_gpio.c")
        require(sdk / "Templates/PY32F002Bxx_Templates_LL/MDK-ARM/startup_py32f002bxx.s")
        checksum(root / pins["puya_pack"]["path"], pins["puya_pack"]["sha256"])

    if args.voice:
        v = pins["voice"]
        sdk = root / v["sdk_path"]
        version("RISC-V GCC", [root / v["gcc_path"], "--version"], "(GCC) " + v["gcc_version"])
        version("GNU Make", [sdk / "tools/build-tools/bin/make.exe", "--version"], "GNU Make " + v["make_version"])
        version("Lua", [sdk / "tools/build-tools/bin/lua.exe", "-v"], "Lua " + v["lua_version"])
        checksum(sdk / "tools/ci-tool-kit.exe", v["ci_tool_kit_sha256"])
        checksum(sdk / "tools/PACK_UPDATE_TOOL.exe", v["pack_update_tool_sha256"])
        project = "projects/offline_asr_alg_pro_sample/"
        for rel in ("libs/libbnpu_core_alg_pro_null.a", project + "project_file/makefile",
                    project + "project_file/source_file.prj", project + "app/app_main/user_config.h",
                    project + "firmware/config.ini",
                    project + "firmware/user_file/cmd_info/[60000]{cmd_info}.xlsx",
                    project + "firmware/asr/[0]lm_chinese_CI1306_V00874.dat",
                    project + "firmware/dnn/[0]G3-NLP-CH-S-PRO-V00874.fefixbin458"):
            require(sdk / rel)
        tracked(v["sdk_path"] + "/" + project + "app/app_main/user_config.h")
        report("INFO", "Voice checks do not validate a production wake-word model or run a firmware build.")

    print(f"\nResult: {len(failures)} failure(s), {len(warnings)} warning(s).")
    print("Local dependency checks do not prove that files were committed or pushed; follow the handoff checklist.")
    return int(bool(failures or (args.strict and warnings)))


if __name__ == "__main__":
    raise SystemExit(main())
