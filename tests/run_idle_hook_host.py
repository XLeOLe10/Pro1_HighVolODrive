#!/usr/bin/env python3
"""Test the actual freertos.c idle hook with stub time and stack queries.

No scheduler/IRQ/board simulation: target builds and hardware timing still matter.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    here = Path(__file__).resolve().parent
    repo_default = here.parent
    fallback_gcc = Path(r"D:\FPGA\QuartusII_13.0\quartus\bin64\cygwin\bin\gcc.exe")
    compiler_default = os.environ.get("CC") or shutil.which("gcc")
    if not compiler_default and fallback_gcc.exists():
        compiler_default = str(fallback_gcc)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=repo_default)
    parser.add_argument("--cc", default=compiler_default or "gcc")
    args = parser.parse_args()
    repo = args.repo.resolve()
    source = repo / "Core/Src/freertos.c"
    source_text = source.read_text(encoding="utf-8")
    begin, end = "/* USER CODE BEGIN 2 */", "/* USER CODE END 2 */"
    require(source_text.count(begin) == 1 and source_text.count(end) == 1,
            "Need exactly one Cube-preserved idle-hook block")
    hook = source_text.split(begin, 1)[1].split(end, 1)[0]
    require(len(re.findall(r"\bvoid\s+vApplicationIdleHook\s*\(\s*void\s*\)", hook)) == 1,
            "USER CODE 2 must contain the production idle hook")

    runtime = (repo / "Core/Src/app_runtime.c").read_text(encoding="utf-8")
    require(re.search(r"\buxTaskGetStackHighWaterMark\s*\(", runtime) is None,
            "Slow full stack scan must not remain in app_runtime")
    config = (repo / "Core/Inc/FreeRTOSConfig.h").read_text(encoding="utf-8")
    require(re.search(r"^#define\s+configUSE_IDLE_HOOK\s+1\s*$", config, re.M),
            "FreeRTOS idle hook is not enabled")
    ioc = (repo / "FOC_T.ioc").read_text(encoding="utf-8")
    require("FREERTOS.configUSE_IDLE_HOOK=1" in ioc.splitlines(),
            "CubeMX idle-hook setting is not enabled")
    ip_lines = [line for line in ioc.splitlines() if line.startswith("FREERTOS.IPParameters=")]
    require(len(ip_lines) == 1 and "configUSE_IDLE_HOOK" in ip_lines[0].split("=", 1)[1].split(","),
            "CubeMX IPParameters must retain the idle-hook setting")

    # Use the production initializers; do not recreate their intended values here.
    initializers = []
    for name in ["app_stack_probe_enabled", "app_stack_min_free_bytes",
                 "app_stack_probe_count", "app_stack_probe_last_tick_ms"]:
        matches = re.findall(r"^volatile\s+uint32_t\s+" + name + r"\s*=\s*[^;]+;", runtime, re.M)
        require(len(matches) == 1, "Expected one initializer for " + name)
        line = matches[0]
        if name == "app_stack_probe_enabled":
            line = "#if AXIS_COMMAND_BENCH_ENABLE\n" + line + "\n#endif"
        initializers.append(line)

    compiler = shutil.which(args.cc) or args.cc
    env = os.environ.copy()
    env["PATH"] = str(Path(compiler).resolve().parent) + os.pathsep + env.get("PATH", "")
    flags = ["-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic"]
    print("Production source:", source, flush=True)
    print("Idle hook SHA256:", hashlib.sha256(hook.encode("utf-8")).hexdigest(), flush=True)
    print("PASS: source/configuration checks (no scan in appTask; idle hook enabled)", flush=True)
    with tempfile.TemporaryDirectory(prefix="idle_hook_host_") as temporary:
        build = Path(temporary)
        (build / "idle_hook_under_test.h").write_text(hook, encoding="utf-8", newline="\n")
        (build / "probe_state_under_test.h").write_text("\n".join(initializers) + "\n", encoding="utf-8", newline="\n")
        shutil.copyfile(repo / "Core/Inc/app_policy.h", build / "app_policy.h")
        shutil.copyfile(here / "test_idle_hook_host.c", build / "test_idle_hook_host.c")
        for mode in (0, 1):
            executable = "idle_hook_" + str(mode) + (".exe" if os.name == "nt" else "")
            command = [compiler, *flags, "-DAXIS_COMMAND_BENCH_ENABLE=" + str(mode),
                       "-I.", "test_idle_hook_host.c", "-o", executable]
            subprocess.run(command, cwd=build, env=env, check=True)
            subprocess.run([str(build / executable)], cwd=build, env=env, check=True)


if __name__ == "__main__":
    main()
