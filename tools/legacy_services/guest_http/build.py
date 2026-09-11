"""Build only our ARMv6 Mach-O program in a NEW output directory.

Requires an ARM-capable Clang, classic compatible ld64 and the user's extracted
original libSystem. No SDK implementation is copied or downloaded.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from macho_audit import audit

SOURCE = Path(__file__).resolve().parent


def validate_arm_command(command):
    # Original OS1 libSystem uses r9 as the pthread/TLS pointer even across
    # calls. Callee-save alone is insufficient: our code must never borrow it.
    if command.count("-ffixed-r9") != 1 or "-fcall-saved-r9" in command:
        raise ValueError("own ARM compilation must reserve OS1 pthread register r9")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def build(output, clang, linker, library, diagnostic=False, *, sources=None,
          expected_imports=None, name="iphone1-local-http", headers=None,
          youtube_demo=False):
    # Other independently authored ARMv6 tools can reuse the exact same ABI
    # machinery while declaring their own finite original-library import set.
    if not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9_-]{0,79}", name):
        raise ValueError("invalid own executable name")
    if (sources is None) != (expected_imports is None):
        raise ValueError("custom sources and expected imports must be supplied together")
    sources = [SOURCE / "server.c", SOURCE / "http.c", SOURCE / "standalone_runtime.c"] if sources is None else [Path(p).resolve() for p in sources]
    headers = [SOURCE / "http.h"] if headers is None and expected_imports is None else list(headers or [])
    if not sources or len(sources) > 16 or len({p.stem for p in sources}) != len(sources):
        raise ValueError("one to sixteen own sources with distinct object stems required")
    pin_paths = sources + [Path(p).resolve() for p in headers] + [SOURCE / "build.py", SOURCE / "macho_audit.py"]
    def source_key(path):
        return str(path.relative_to(SOURCE)) if path.is_relative_to(SOURCE) else str(path)
    source_pins = {source_key(p): sha(p) for p in pin_paths}
    output.mkdir(parents=True, exist_ok=False)
    report = {"passed": False, "programName": name, "diagnosticLogging": diagnostic,
              "youtubeDemo": bool(youtube_demo), "commands": [], "sourceSHA256": source_pins}
    try:
        report["clangVersion"] = subprocess.check_output(
            [str(clang), "--version"], text=True, timeout=10)
        objects = []
        commands = []
        for source in sources:
            obj = output / (source.stem + ".o")
            objects.append(obj)
            commands.append([
                str(clang), "-target", "armv6-apple-darwin8", "-marm", "-Os", "-ffixed-r9",
                "-fno-stack-protector", "-fno-builtin", "-fno-unwind-tables",
                "-fno-asynchronous-unwind-tables", "-fno-pic",
                "-DOWN_HTTP_DIAGNOSTICS=" + str(int(diagnostic)),
                "-DOWN_YOUTUBE_DEMO=" + str(int(bool(youtube_demo))),
                "-c", str(source), "-o", str(obj)])
            validate_arm_command(commands[-1])
        executable = output / name
        commands.append([
            str(linker), "-arch", "armv6", "-ios_version_min", "1.0", "-e", "_start",
            "-no_pie", "-no_uuid", "-no_compact_unwind", "-no_version_load_command",
            "-no_function_starts", "-no_data_in_code_info", "-no_encryption",
            "-o", str(executable), *map(str, objects), str(library)])
        for command in commands:
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            report["commands"].append({"argv": command, "returncode": result.returncode,
                                       "stdout": result.stdout, "stderr": result.stderr})
            result.check_returncode()
        report["audit"] = audit(executable, library, expected_imports)
        if source_pins != {source_key(p): sha(p) for p in pin_paths}:
            raise ValueError("own sources changed during build")
        report["toolSHA256"] = {"clang": sha(clang), "linker": sha(linker),
                                "originalLibrary": sha(library)}
        report["passed"] = True
        return report
    except BaseException as error:
        report["error"] = repr(error)
        raise
    finally:
        (output / "build.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--clang", type=Path, required=True)
    parser.add_argument("--ld64", type=Path, required=True)
    parser.add_argument("--libsystem", type=Path, required=True)
    parser.add_argument("--diagnostic", action="store_true")
    parser.add_argument("--youtube-demo", action="store_true",
                        help="Enable explicitly local YouTube catalog/media fixture")
    parser.add_argument("--source", type=Path, action="append", help="Custom own C source; requires --import-symbol")
    parser.add_argument("--header", type=Path, action="append", help="Additional own header to pin")
    parser.add_argument("--import-symbol", action="append", help="Expected original Mach-O symbol, including leading underscore")
    parser.add_argument("--name", default="iphone1-local-http")
    args = parser.parse_args()
    print(json.dumps(build(args.output.resolve(), args.clang.resolve(),
                           args.ld64.resolve(), args.libsystem.resolve(), args.diagnostic,
                           sources=args.source, expected_imports=args.import_symbol,
                           name=args.name, headers=args.header,
                           youtube_demo=args.youtube_demo), indent=2))
