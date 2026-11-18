"""Exercise the production app_runtime startup-identity block with injected RNG outcomes.

One test-only runner; no extra firmware module. Does not model the RNG entropy
source, HAL polling duration, STM32 startup electrical state or IRQ latency.
"""
from pathlib import Path
import argparse
import hashlib
import os
import subprocess

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_runtime.h"

typedef struct { uint32_t CR, SR; } HostRng;
typedef struct {
    HostRng *Instance;
    uint32_t State, ErrorCode;
    struct { uint32_t ClockErrorDetection; } Init;
} RNG_HandleTypeDef;
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
#define HAL_RNG_STATE_READY 1U
#define HAL_RNG_ERROR_NONE 0U
#define HAL_RNG_ERROR_TIMEOUT 2U
#define HAL_RNG_ERROR_BUSY 4U
#define RNG_CED_ENABLE 0U
#define RNG_CR_RNGEN (1U << 2)
#define RNG_CR_IE (1U << 3)
#define RNG_CR_CED (1U << 5)
#define RNG_SR_CECS (1U << 1)
#define RNG_SR_SECS (1U << 2)
#define RNG_SR_CEIS (1U << 5)
#define RNG_SR_SEIS (1U << 6)
static HostRng hardware;
#define RNG (&hardware)
static RNG_HandleTypeDef hrng;
static unsigned assertions, calls;
static uint32_t mask, ipsr;
static uint32_t source[5];
static uint32_t inject_at, inject_sr, inject_error, inject_state;
static HAL_StatusTypeDef inject_result;
static uint32_t __get_PRIMASK(void) { return mask; }
static uint32_t __get_IPSR(void) { return ipsr; }
#define CHECK(x) do { assertions++; if (!(x)) { \
 fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static HAL_StatusTypeDef HAL_RNG_GenerateRandomNumber(RNG_HandleTypeDef *, uint32_t *);

#include "runtime_boot_under_test.h"

static void CheckWords(uint32_t state, const uint32_t *expected)
{
    uint32_t words[4] = {99U, 99U, 99U, 99U};
    unsigned i;
    CHECK(AppRuntime_GetBootIdentity(words) == state);
    CHECK(AppRuntime_GetBootIdentity(NULL) == state);
    for (i = 0U; i < 4U; ++i) CHECK(words[i] == (expected ? expected[i] : 0U));
}

static HAL_StatusTypeDef HAL_RNG_GenerateRandomNumber(RNG_HandleTypeDef *handle,
                                                     uint32_t *word)
{
    CHECK(handle == &hrng);
    CHECK(calls < 5U);
    /* Inspect while candidate is being assembled: no partial word escapes. */
    CheckWords(APP_BOOT_STATE_NOT_READY, NULL);
    *word = source[calls++];
    if (calls == inject_at) {
        hardware.SR = inject_sr;
        hrng.ErrorCode = inject_error;
        hrng.State = inject_state;
        return inject_result;
    }
    return HAL_OK;
}

static void Reset(void)
{
    static const uint32_t initial[5] = {
        0x01234567U, 0x89ABCDEFU, 0x76543210U, 0xFEEDBEEFU, 0xA1B2C3D4U
    };
    /* Model a new MCU reset; production has no public identity-reset API. */
    boot_identity_attempted = 0U;
    boot_identity_state = APP_BOOT_STATE_NOT_READY;
    memset(boot_identity_words, 0, sizeof boot_identity_words);
    app_boot_identity_state = APP_BOOT_STATE_NOT_READY;
    app_boot_error_flags = 0U;
    hardware.CR = RNG_CR_RNGEN;
    hardware.SR = 0U;
    hrng.Instance = RNG;
    hrng.State = HAL_RNG_STATE_READY;
    hrng.ErrorCode = HAL_RNG_ERROR_NONE;
    hrng.Init.ClockErrorDetection = RNG_CED_ENABLE;
    mask = ipsr = calls = inject_at = inject_sr = inject_error = 0U;
    inject_state = HAL_RNG_STATE_READY;
    inject_result = HAL_OK;
    memcpy(source, initial, sizeof source);
}

static void CheckFailure(uint32_t error)
{
    unsigned before = calls;
    CHECK(app_boot_identity_state == APP_BOOT_STATE_FAILED);
    CHECK((app_boot_error_flags & error) != 0U);
    CheckWords(APP_BOOT_STATE_FAILED, NULL);
    /* No retry within a boot, even after hardware looks healthy again. */
    hardware.SR = 0U;
    hrng.ErrorCode = HAL_RNG_ERROR_NONE;
    hrng.State = HAL_RNG_STATE_READY;
    InitBootIdentity();
    CHECK(calls == before);
    CheckWords(APP_BOOT_STATE_FAILED, NULL);
    /* Debugger-writable diagnostics must not become the source of truth. */
    app_boot_identity_state = APP_BOOT_STATE_VALID;
    app_boot_error_flags = 0U;
    CheckWords(APP_BOOT_STATE_FAILED, NULL);
}

int main(void)
{
    unsigned i, j, mode;
    const uint32_t sr_flags[4] = {RNG_SR_CECS, RNG_SR_CEIS, RNG_SR_SECS, RNG_SR_SEIS};
    uint32_t old[4], now[4];
    Reset();
    CheckWords(APP_BOOT_STATE_NOT_READY, NULL);
    InitBootIdentity();
    CHECK(calls == 5U); /* First word discarded, four retained. */
    CHECK(app_boot_error_flags == 0U);
    CheckWords(APP_BOOT_STATE_VALID, &source[1]);
    for (i = 0U; i < 100U; ++i) CheckWords(APP_BOOT_STATE_VALID, &source[1]);
    InitBootIdentity();
    CHECK(calls == 5U);
    AppRuntime_GetBootIdentity(old);
    app_boot_identity_state = APP_BOOT_STATE_FAILED;
    CheckWords(APP_BOOT_STATE_VALID, old);
    memset(now, 0, sizeof now);
    AppRuntime_GetBootIdentity(now);
    now[0] = 0U; /* Caller owns the copy, not the underlying identity. */
    CheckWords(APP_BOOT_STATE_VALID, old);
    Reset();
    source[4] ^= 0x1234U;
    InitBootIdentity();
    CHECK(AppRuntime_GetBootIdentity(now) == APP_BOOT_STATE_VALID);
    CHECK(memcmp(old, now, sizeof old) != 0);

    for (mode = 0U; mode < 9U; ++mode) {
        Reset();
        if (mode == 0U) hrng.Instance = NULL;
        if (mode == 1U) hrng.State = 99U;
        if (mode == 2U) hrng.ErrorCode = HAL_RNG_ERROR_BUSY;
        if (mode == 3U) hrng.Init.ClockErrorDetection = 1U;
        if (mode == 4U) mask = 1U;
        if (mode == 5U) ipsr = 25U;
        if (mode == 6U) hardware.CR = 0U;
        if (mode == 7U) hardware.CR |= RNG_CR_IE;
        if (mode == 8U) hardware.CR |= RNG_CR_CED;
        InitBootIdentity();
        CHECK(calls == 0U);
        CheckFailure(APP_BOOT_ERROR_CONFIG);
    }
    for (j = 0U; j < 4U; ++j) {
        uint32_t error = (j < 2U) ? APP_BOOT_ERROR_CLOCK : APP_BOOT_ERROR_SEED;
        Reset();
        hardware.SR = sr_flags[j];
        InitBootIdentity();
        CHECK(calls == 0U);
        CheckFailure(error);
        for (i = 1U; i <= 5U; ++i) {
            Reset();
            inject_at = i;
            inject_sr = sr_flags[j];
            InitBootIdentity();
            CHECK(calls == i);
            CheckFailure(error); /* HAL_OK with bad SR must fail. */
        }
    }
    for (i = 1U; i <= 5U; ++i) {
        for (mode = 0U; mode < 5U; ++mode) {
            Reset();
            inject_at = i;
            if (mode == 0U) { inject_result = HAL_ERROR; inject_error = HAL_RNG_ERROR_TIMEOUT; }
            if (mode == 1U) inject_result = HAL_BUSY;
            if (mode == 2U) inject_result = HAL_TIMEOUT;
            if (mode == 3U) inject_error = HAL_RNG_ERROR_BUSY; /* HAL_OK, stale error */
            if (mode == 4U) inject_state = 99U;
            InitBootIdentity();
            CHECK(calls == i);
            if (mode == 0U) CHECK((app_boot_error_flags & APP_BOOT_ERROR_TIMEOUT) != 0U);
            CheckFailure(APP_BOOT_ERROR_HAL);
        }
    }
    for (i = 1U; i < 5U; ++i) {
        Reset();
        source[i] = source[i - 1U];
        InitBootIdentity();
        CHECK(calls == i + 1U);
        CheckFailure(APP_BOOT_ERROR_REPEAT);
    }
    Reset();
    memset(source, 0, sizeof source);
    InitBootIdentity();
    CheckFailure(APP_BOOT_ERROR_ZERO); /* Also rejects the all-zero identity. */
    for (i = 0U; i < 5U; ++i) {
        Reset();
        source[i] = 0U; /* RM recommends rejecting every zero DR word. */
        InitBootIdentity();
        CHECK(calls == i + 1U);
        CheckFailure(APP_BOOT_ERROR_ZERO);
    }
    Reset();
    source[4] = source[1]; /* Non-adjacent equality is not a continuous failure. */
    InitBootIdentity();
    CHECK(app_boot_error_flags == 0U);
    CheckWords(APP_BOOT_STATE_VALID, &source[1]);
    printf("PASS: %u startup identity assertions (actual runtime block)\n", assertions);
    return 0;
}
"""

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=r"D:\FPGA\QuartusII_13.0\quartus\bin64\cygwin\bin\gcc.exe")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    out = repo / "build/008-can-boot-identity"
    out.mkdir(parents=True, exist_ok=True)
    source = (repo / "Core/Src/app_runtime.c").read_text(encoding="utf-8")
    begin = "/* BOOT IDENTITY BEGIN: tested from this production block on the host. */"
    end = "/* BOOT IDENTITY END */"
    assert source.count(begin) == source.count(end) == 1
    block = source.split(begin, 1)[1].split(end, 1)[0]
    assert source.count("    InitBootIdentity();") == 1
    main_source = (repo / "Core/Src/main.c").read_text(encoding="utf-8")
    assert main_source.index("  MX_RNG_Init();") < main_source.index("  AppRuntime_Init();")
    assert main_source.index("  AppRuntime_Init();") < main_source.index("  osKernelInitialize();")
    (out / "runtime_boot_under_test.h").write_text(block, encoding="utf-8", newline="\n")
    (out / "runtime_boot_host.c").write_text(HARNESS, encoding="utf-8", newline="\n")
    env = os.environ.copy()
    env["PATH"] = str(Path(args.compiler).parent) + os.pathsep + env.get("PATH", "")
    exe = out / "runtime_boot_host.exe"
    command = [args.compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
               "-I", str(repo / "Core/Inc"), "-I", str(out), str(out / "runtime_boot_host.c"),
               "-o", str(exe)]
    log = ["Production block SHA256: " + hashlib.sha256(block.encode()).hexdigest() + "\n"]
    for cmd in (command, [str(exe)]):
        proc = subprocess.run(cmd, cwd=repo, env=env, text=True, errors="replace",
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log += ["COMMAND: "+subprocess.list2cmdline(cmd)+"\n", proc.stdout,
                "EXIT: "+str(proc.returncode)+"\n"]
        (out/"runtime-host.txt").write_text("".join(log), encoding="utf-8", newline="\n")
        if proc.returncode:
            raise RuntimeError(proc.stdout)
        print(proc.stdout.strip())
    print("PASS: startup call ordering; no board/entropy-quality/physical timing proof.")

if __name__ == "__main__":
    main()
