#!/usr/bin/env python3
"""Compile production telemetry_format.c together with its independent host test."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    here = Path(__file__).resolve().parent
    staged_source = here / "telemetry_format.c"
    source_default = staged_source if staged_source.exists() else here.parent / "Core" / "Src" / "telemetry_format.c"
    include_default = here if staged_source.exists() else here.parent / "Core" / "Inc"
    fallback_gcc = Path(r"D:\FPGA\QuartusII_13.0\quartus\bin64\cygwin\bin\gcc.exe")
    compiler_default = os.environ.get("CC") or shutil.which("gcc")
    if not compiler_default and fallback_gcc.exists():
        compiler_default = str(fallback_gcc)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=source_default)
    parser.add_argument("--include", type=Path, default=include_default)
    parser.add_argument("--cc", default=compiler_default or "gcc")
    parser.add_argument("--sanitize", action="store_true", help="Enable ASan/UBSan if supported by the selected host compiler")
    args = parser.parse_args()
    source = args.source.resolve()
    include = args.include.resolve()
    if not source.is_file():
        parser.error("Production source does not exist: " + str(source))
    if not (include / "telemetry_format.h").is_file():
        parser.error("Production header does not exist in: " + str(include))
    compiler = shutil.which(args.cc) or args.cc
    env = os.environ.copy()
    env["PATH"] = str(Path(compiler).resolve().parent) + os.pathsep + env.get("PATH", "")
    flags = ["-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    # Build a copy of the exact production files in a temporary ASCII path:
    # legacy Windows/Cygwin GCC cannot consistently open Unicode directories.
    with tempfile.TemporaryDirectory(prefix="telemetry_format_host_") as temporary:
        build = Path(temporary)
        shutil.copyfile(source, build / "telemetry_format.c")
        shutil.copyfile(include / "telemetry_format.h", build / "telemetry_format.h")
        shutil.copyfile(here / "test_telemetry_format.c", build / "test_telemetry_format.c")
        executable = "test_telemetry_format.exe" if os.name == "nt" else "test_telemetry_format"
        command = [compiler, *flags, "-I.", "telemetry_format.c", "test_telemetry_format.c", "-lm", "-o", executable]
        print("Production source:", source, flush=True)
        print("Compiler:", compiler, flush=True)
        subprocess.run(command, cwd=build, env=env, check=True)
        subprocess.run([str(build / executable)], cwd=build, env=env, check=True)


if __name__ == "__main__":
    main()
