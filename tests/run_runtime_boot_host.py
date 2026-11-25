"""Exercise production startup/diagnostic, feedback timing, task-wait and ADC blocks.

One test-only runner; no extra firmware module. Does not model the RNG entropy
source, HAL polling duration, STM32 startup electrical state or IRQ latency.
"""
from pathlib import Path
import argparse
import hashlib
import os
import re
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

READY_LOSS_HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_runtime.h"
#include "app_policy.h"
#include "ad2s1210.h"
#include "axis_command_core.h"

static unsigned assertions, barriers, arm_calls, pair_assertions;
static uint32_t armed_check;
static const AD2S1210_FeedbackSnapshot *armed_snapshot;
#define PAIR_CHECK(x) do { pair_assertions++; if (!(x)) { \
 fprintf(stderr, "FAIL ready pairing line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
void AD2S1210_ArmAgeTrace(const AD2S1210_FeedbackSnapshot *snapshot, uint32_t check)
{
    PAIR_CHECK(app_ready_loss_trace.captured == 0U);
    arm_calls++;
    armed_snapshot = snapshot;
    armed_check = check;
}
#define CHECK(x) do { assertions++; if (!(x)) { \
 fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static uint32_t feedback_age_limit_cycles;
volatile AppReadyLossTrace app_ready_loss_trace;
static volatile uint32_t app_task_phase;
volatile uint32_t axis_command_result, axis_command_epoch, axis_command_result_seq;
volatile uint32_t ad2s_sample_gap_count, ad2s_spi_error_count, app_task_deadline_misses;
static void HostDmb(void);
#define __DMB() HostDmb()

#include "runtime_ready_loss_under_test.h"

static void HostDmb(void)
{
    /* A reader must never see captured=1 before the payload is published. */
    CHECK(app_ready_loss_trace.captured == 0U);
    barriers++;
}
static AD2S1210_FeedbackSnapshot sample;
static uint32_t current_ready, lockout, fault, now_cycles, now_ms;
static void Reset(void)
{
    AppReadyLossTrace cleared;
    memset(&cleared, 0, sizeof cleared);
    app_ready_loss_trace = cleared; /* Model a new boot, not a production re-arm. */
    memset(&sample, 0, sizeof sample);
    sample.sample_cycles = 100000U; sample.sample_tick_ms = 100U;
    sample.sequence = 17U; sample.valid = sample.speed_valid = 1U;
    now_cycles = 300000U; now_ms = 101U;
    feedback_age_limit_cycles = 400000U;
    current_ready = 1U; lockout = fault = 0U;
    axis_command_result = AXIS_CMD_NOT_READY;
    axis_command_epoch = 8U; axis_command_result_seq = 77U;
    app_task_phase = 3U;
    ad2s_sample_gap_count = 0U; ad2s_spi_error_count = 5U; app_task_deadline_misses = 6U;
    arm_calls = 0U; armed_snapshot = NULL; armed_check = 0U;
    barriers = 0U;
}
static uint32_t Ready(void)
{
    uint32_t feedback = sample.valid && sample.speed_valid &&
        APP_FEEDBACK_FRESH(now_ms, sample.sample_tick_ms, now_cycles,
                           sample.sample_cycles, feedback_age_limit_cycles);
    return (uint32_t)APP_CAN_ENABLE(1U, 1U, current_ready, feedback, fault, lockout);
}
static void Capture(uint32_t previous_permit, uint32_t epoch_before)
{
    CaptureReadyLoss(previous_permit, epoch_before, Ready(), now_cycles, now_ms,
                     &sample, current_ready, lockout, fault);
}
static void CheckNone(void)
{
    AppReadyLossTrace value = app_ready_loss_trace;
    AppReadyLossTrace cleared;
    memset(&cleared, 0, sizeof cleared);
    CHECK(memcmp(&value, &cleared, sizeof value) == 0);
    CHECK(barriers == 0U);
    PAIR_CHECK(arm_calls == 0U);
}
static void CheckTrace(uint32_t reasons, uint32_t age_cycles, uint32_t age_ms)
{
    CHECK(app_ready_loss_trace.captured == 1U && barriers == 1U);
    CHECK(app_ready_loss_trace.reason_bits == reasons);
    if ((reasons & APP_READY_LOSS_DWT_AGE) != 0U) {
        PAIR_CHECK(arm_calls == 1U && armed_snapshot == &sample && armed_check == now_cycles);
    } else {
        PAIR_CHECK(arm_calls == 0U);
    }
    CHECK(app_ready_loss_trace.age_cycles == age_cycles && app_ready_loss_trace.age_ms == age_ms);
    CHECK(app_ready_loss_trace.sample_seq == sample.sequence);
    CHECK(app_ready_loss_trace.command_seq == 77U);
    CHECK(app_ready_loss_trace.epoch_before == 7U && app_ready_loss_trace.epoch_after == 8U);
    CHECK(app_ready_loss_trace.task_phase == app_task_phase);
    CHECK(app_ready_loss_trace.gap_count == ad2s_sample_gap_count);
    CHECK(app_ready_loss_trace.spi_errors == ad2s_spi_error_count);
    CHECK(app_ready_loss_trace.deadline_misses == app_task_deadline_misses);
    /* The diagnostic must not repair the cause or alter command authority. */
    CHECK(axis_command_result == AXIS_CMD_NOT_READY && axis_command_epoch == 8U && axis_command_result_seq == 77U);
}
int main(void)
{
    static const uint32_t reasons[] = {APP_READY_LOSS_POSITION, APP_READY_LOSS_SPEED,
        APP_READY_LOSS_DWT_AGE, APP_READY_LOSS_HAL_AGE, APP_READY_LOSS_CURRENT,
        APP_READY_LOSS_LOCKOUT, APP_READY_LOSS_FAULT};
    static const uint32_t other_results[] = {AXIS_CMD_OK, AXIS_CMD_APPLIED, AXIS_CMD_EXPIRED, AXIS_CMD_FAULT};
    unsigned mode;
    AppReadyLossTrace frozen, observed;
    Reset(); sample.valid = 0U;
    Capture(0U, 7U); CheckNone(); /* Startup never had an active permission. */
    Reset(); Capture(1U, 7U); CheckNone(); /* Ready remains true. */
    for (mode = 0U; mode < sizeof(other_results)/sizeof(other_results[0]); mode++) {
        Reset(); sample.valid = 0U; axis_command_result = other_results[mode];
        Capture(1U, 7U); CheckNone();
    }
    Reset(); sample.valid = 0U; Capture(1U, 8U); CheckNone(); /* No epoch transition. */
    Reset(); sample.fault = 0xFFFFU; Capture(1U, 7U); CheckNone(); /* Device fault is separate policy. */

    for (mode = 0U; mode < sizeof(reasons)/sizeof(reasons[0]); mode++) {
        Reset();
        if (mode == 0U) sample.valid = 0U;
        if (mode == 1U) sample.speed_valid = 0U;
        if (mode == 2U) { now_cycles = sample.sample_cycles + 400001U; now_ms = 102U; }
        if (mode == 3U) now_ms = 103U;
        if (mode == 4U) current_ready = 0U;
        if (mode == 5U) lockout = 1U;
        if (mode == 6U) fault = 0x20U;
        CHECK(Ready() == 0U); Capture(1U, 7U);
        CheckTrace(reasons[mode], now_cycles - sample.sample_cycles, now_ms - sample.sample_tick_ms);
    }
    Reset(); now_cycles = sample.sample_cycles + 400000U; now_ms = 102U;
    CHECK(Ready() == 1U); Capture(1U, 7U); CheckNone(); /* Inclusive 2 ms boundary. */
    current_ready = 0U; Capture(1U, 7U);
    CheckTrace(APP_READY_LOSS_CURRENT, 400000U, 2U); /* No spurious age reason at boundary. */

    Reset(); now_cycles = sample.sample_cycles + 400001U; now_ms = 102U;
    ad2s_sample_gap_count = 0U; ad2s_spi_error_count = 0U;
    Capture(1U, 7U); CheckTrace(APP_READY_LOSS_DWT_AGE, 400001U, 2U);
    CHECK(app_ready_loss_trace.gap_count == 0U && app_ready_loss_trace.spi_errors == 0U);

    Reset(); sample.sample_cycles = UINT32_MAX - 100U; sample.sample_tick_ms = UINT32_MAX;
    now_cycles = sample.sample_cycles + 400000U; now_ms = 1U;
    CHECK(Ready() == 1U); Capture(1U, 7U); CheckNone();
    now_cycles++; Capture(1U, 7U); CheckTrace(APP_READY_LOSS_DWT_AGE, 400001U, 2U);
    Reset(); sample.sample_cycles = UINT32_MAX - 100U; sample.sample_tick_ms = UINT32_MAX;
    now_cycles = sample.sample_cycles + 32U; now_ms = 2U;
    Capture(1U, 7U); CheckTrace(APP_READY_LOSS_HAL_AGE, 32U, 3U);

    Reset(); sample.valid = sample.speed_valid = 0U;
    now_cycles = sample.sample_cycles + 400001U; now_ms = 103U;
    current_ready = 0U; lockout = 1U; fault = 0x20U;
    ad2s_sample_gap_count = 9U; ad2s_spi_error_count = 10U; app_task_deadline_misses = 11U;
    Capture(1U, 7U);
    CheckTrace(APP_READY_LOSS_POSITION | APP_READY_LOSS_SPEED | APP_READY_LOSS_DWT_AGE |
        APP_READY_LOSS_HAL_AGE | APP_READY_LOSS_CURRENT | APP_READY_LOSS_LOCKOUT |
        APP_READY_LOSS_FAULT, 400001U, 3U);
    frozen = app_ready_loss_trace;
    sample.sequence++; sample.sample_cycles++; sample.sample_tick_ms++;
    axis_command_epoch = 10U; axis_command_result_seq = 78U;
    app_task_phase = 4U; ad2s_sample_gap_count++; ad2s_spi_error_count++; app_task_deadline_misses++;
    Capture(1U, 9U); observed = app_ready_loss_trace;
    CHECK(memcmp(&frozen, &observed, sizeof frozen) == 0 && barriers == 1U);
    PAIR_CHECK(arm_calls == 1U);
    printf("PASS: %u ready-loss diagnostic assertions (actual runtime block)\n", assertions);
    printf("PASS: %u ready-loss pairing handoff assertions\n", pair_assertions);
    return 0;
}
"""


ADC_PHASE_HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "current_sense.h"

/* No JDR member: the extracted flag/diagnostic block cannot read sample data. */
typedef struct { volatile uint32_t ISR; } HostAdc;
typedef struct { HostAdc *Instance; } HostAdcHandle;
typedef struct { volatile uint32_t CYCCNT; } HostDwt;
typedef struct { volatile uint32_t CR1, CNT, SR; } HostTim;
#define ADC_FLAG_JEOC (1U << 5)
#define ADC_FLAG_JEOS (1U << 6)
#define TIM_CR1_DIR (1U << 4)
static HostAdc adc[3];
static HostAdcHandle handles[3];
static HostDwt dwt;
static HostTim timer;
static unsigned assertions, events[32], event_count, clear_count;
static unsigned dwt_reads, tim_reads, barriers;
static uint32_t dir_flip;
static CurrentSenseFaultTrace at_barrier;
#define CHECK(x) do { assertions++; if (!(x)) { \
 fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static HostAdcHandle *HostHandle(unsigned id);
static void HostClear(HostAdcHandle *handle, uint32_t flags);
static HostDwt *HostDwtRead(void) { dwt_reads++; return &dwt; }
static HostTim *HostTimRead(void)
{
    tim_reads++;
    if (tim_reads == 3U) timer.CR1 ^= dir_flip;
    return &timer;
}
static void HostDmb(void);
#define hadc1 (*HostHandle(1U))
#define hadc2 (*HostHandle(2U))
#define hadc3 (*HostHandle(3U))
#define DWT HostDwtRead()
#define TIM1 HostTimRead()
#define __HAL_ADC_CLEAR_FLAG(h, f) HostClear((h), (f))
#define __DMB() HostDmb()

#include "current_adc_phase_under_test.h"

static HostAdcHandle *HostHandle(unsigned id)
{
    CHECK(id >= 1U && id <= 3U && event_count < 32U);
    events[event_count++] = id;
    return &handles[id - 1U];
}
static void HostClear(HostAdcHandle *handle, uint32_t flags)
{
    unsigned id = (unsigned)(handle - handles) + 1U;
    CHECK(flags == (ADC_FLAG_JEOS | ADC_FLAG_JEOC));
    CHECK(event_count < 32U);
    events[event_count++] = 100U + id;
    handle->Instance->ISR &= ~flags; /* Model W1C, including unrelated-bit preservation. */
    clear_count++;
}
static void HostDmb(void)
{
    CHECK(current_sense_fault_trace.captured == 0U);
    at_barrier = current_sense_fault_trace;
    barriers++;
}
static void ClearObservations(void)
{
    event_count = clear_count = dwt_reads = tim_reads = 0U;
}
static void Reset(void)
{
    CurrentSenseFaultTrace empty;
    unsigned i;
    memset(&empty, 0, sizeof empty);
    current_sense_fault_trace = empty;
    memset(&missing_check, 0, sizeof missing_check);
    previous_check_cycles = previous_check_valid = 0U;
    previous_success_check_cycles = previous_success_check_valid = 0U;
    for (i = 0U; i < 3U; i++) {
        adc[i].ISR = ADC_FLAG_JEOS | ADC_FLAG_JEOC | (1U << (10U + i));
        handles[i].Instance = &adc[i];
    }
    dwt.CYCCNT = 100U;
    timer.CR1 = 0x20U; timer.CNT = 0x12345U; timer.SR = 0x99U;
    dir_flip = 0U; barriers = 0U;
    ClearObservations();
}
static void CheckUncaptured(void)
{
    CurrentSenseFaultTrace empty, observed = current_sense_fault_trace;
    memset(&empty, 0, sizeof empty);
    CHECK(memcmp(&observed, &empty, sizeof empty) == 0 && barriers == 0U);
}
static void CheckPublished(void)
{
    CurrentSenseFaultTrace observed = current_sense_fault_trace;
    CHECK(observed.captured == 1U && barriers == 1U);
    observed.captured = 0U;
    /* All payload fields were already final at the barrier preceding captured. */
    CHECK(memcmp(&observed, &at_barrier, sizeof observed) == 0);
}
static void CheckReady(void)
{
    static const unsigned order[] = {1U, 2U, 3U, 1U, 101U, 2U, 102U, 3U, 103U};
    unsigned i;
    ClearObservations();
    CHECK(CurrentSense_HasNewSample() == 1U);
    CHECK(event_count == sizeof order / sizeof order[0]);
    for (i = 0U; i < event_count; i++) CHECK(events[i] == order[i]);
    CHECK(clear_count == 3U && dwt_reads == 1U && tim_reads == 0U);
    for (i = 0U; i < 3U; i++) CHECK(adc[i].ISR == (1U << (10U + i)));
    CHECK(missing_check.first_missing_adc == 0U);
}
int main(void)
{
    unsigned missing, i, mode;
    uint32_t original[3];
    CurrentSenseFaultTrace frozen, observed;
    CHECK(sizeof(CurrentSenseFaultTrace) == 11U * sizeof(uint32_t));
    Reset(); CurrentSense_CaptureFault(90U, 80U, 7U, 1U, 77U);
    CheckUncaptured(); CHECK(event_count == 0U && tim_reads == 0U && dwt_reads == 0U);
    for (missing = 1U; missing <= 3U; missing++) {
        Reset(); adc[missing - 1U].ISR &= ~ADC_FLAG_JEOS;
        for (i = 0U; i < 3U; i++) original[i] = adc[i].ISR;
        CHECK(CurrentSense_HasNewSample() == 0U);
        CHECK(event_count == missing && clear_count == 0U);
        for (i = 0U; i < missing; i++) CHECK(events[i] == i + 1U);
        for (i = 0U; i < 3U; i++) CHECK(adc[i].ISR == original[i]);
        CHECK(dwt_reads == 1U && tim_reads == 4U);
        CheckUncaptured(); /* Scratch observation is not the public frozen fault. */
        CurrentSense_CaptureFault(90U, 80U, 77U, 0U, 55U);
        CheckPublished();
        CHECK(event_count == missing && tim_reads == 4U && dwt_reads == 1U);
        CHECK(current_sense_fault_trace.first_missing_adc == missing);
        CHECK(current_sense_fault_trace.missing_isr == original[missing - 1U]);
        CHECK(current_sense_fault_trace.check_interval_cycles == UINT32_MAX);
        CHECK(current_sense_fault_trace.since_success_cycles == UINT32_MAX);
        CHECK(current_sense_fault_trace.body_entry_interval_cycles == UINT32_MAX);
        CHECK(current_sense_fault_trace.previous_body_cycles == UINT32_MAX);
        CHECK(current_sense_fault_trace.entry_to_check_cycles == 10U);
        CHECK(current_sense_fault_trace.tim_phase == 0x2345U);
        CHECK(current_sense_fault_trace.tim_sr == 0x99U);
        CHECK(current_sense_fault_trace.command_seq == 55U);
        frozen = current_sense_fault_trace;
        ClearObservations(); adc[0].ISR &= ~ADC_FLAG_JEOS; dwt.CYCCNT = 500U;
        CHECK(CurrentSense_HasNewSample() == 0U);
        CurrentSense_CaptureFault(400U, 90U, 333U, 1U, 66U);
        observed = current_sense_fault_trace;
        CHECK(memcmp(&frozen, &observed, sizeof frozen) == 0 && barriers == 1U);
    }
    Reset(); CheckReady();
    CurrentSense_CaptureFault(90U, 80U, 7U, 1U, 77U); CheckUncaptured();
    Reset(); adc[0].ISR &= ~ADC_FLAG_JEOS;
    CHECK(CurrentSense_HasNewSample() == 0U);
    adc[0].ISR |= ADC_FLAG_JEOS; dwt.CYCCNT = 150U;
    CheckReady(); /* A good check removes the earlier scratch miss. */
    CurrentSense_CaptureFault(140U, 90U, 7U, 1U, 77U); CheckUncaptured();

    Reset(); dwt.CYCCNT = UINT32_MAX - 15U; CheckReady();
    ClearObservations(); dwt.CYCCNT = 32U; /* DWT wrap across one successful check. */
    CHECK(CurrentSense_HasNewSample() == 0U);
    CurrentSense_CaptureFault(8U, UINT32_MAX - 31U, 25U, 1U, 77U);
    CheckPublished();
    CHECK(current_sense_fault_trace.check_interval_cycles == 48U);
    CHECK(current_sense_fault_trace.since_success_cycles == 48U);
    CHECK(current_sense_fault_trace.body_entry_interval_cycles == 40U);
    CHECK(current_sense_fault_trace.entry_to_check_cycles == 24U);
    CHECK(current_sense_fault_trace.previous_body_cycles == 25U);

    Reset(); adc[0].ISR &= ~ADC_FLAG_JEOS;
    CHECK(CurrentSense_HasNewSample() == 0U);
    adc[0].ISR |= ADC_FLAG_JEOS; adc[1].ISR &= ~ADC_FLAG_JEOS;
    ClearObservations(); dwt.CYCCNT = 190U;
    CHECK(CurrentSense_HasNewSample() == 0U);
    CurrentSense_CaptureFault(180U, 90U, 14U, 1U, 78U);
    CheckPublished(); /* Freeze the latest actual first-missing decision. */
    CHECK(current_sense_fault_trace.first_missing_adc == 2U);
    CHECK(current_sense_fault_trace.check_interval_cycles == 90U);
    CHECK(current_sense_fault_trace.since_success_cycles == UINT32_MAX);
    CHECK(current_sense_fault_trace.body_entry_interval_cycles == 90U);

    for (mode = 0U; mode < 5U; mode++) {
        uint32_t expected_phase = 0x2345U;
        Reset(); adc[0].ISR &= ~ADC_FLAG_JEOS;
        if (mode == 1U || mode == 3U) timer.CR1 |= TIM_CR1_DIR;
        if (mode == 2U || mode == 3U) dir_flip = TIM_CR1_DIR;
        if (mode == 4U) dir_flip = 0x20U; /* An unrelated CR1 change is not DIR uncertainty. */
        if (mode == 1U || mode == 2U) expected_phase |= (1U << 16);
        if (mode == 2U || mode == 3U) expected_phase |= (1U << 31);
        CHECK(CurrentSense_HasNewSample() == 0U);
        CurrentSense_CaptureFault(90U, 80U, 7U, 1U, 77U); CheckPublished();
        CHECK(current_sense_fault_trace.tim_phase == expected_phase);
        CHECK(current_sense_fault_trace.tim_sr == timer.SR && tim_reads == 4U);
    }
    printf("PASS: %u ADC phase diagnostic assertions (actual current_sense block)\n", assertions);
    return 0;
}
"""


def check_adc_runtime_order(repo, source):
    """Source-order evidence only: this is not an execution/IRQ interleaving model."""
    assert source.count("CurrentSense_CaptureFault(") == 1
    fault_start = source.index("    if ((board_output_enabled != 0U) || (command_permit != 0U))")
    capture_at = source.index("CurrentSense_CaptureFault(", fault_start)
    gate = source[fault_start:capture_at]
    positions = [gate.index(text) for text in (
        "if (sample_ok == 0U) reason |= BOARD_FAULT_ADC_FRAME;",
        "uint32_t capture_adc_fault =", "BoardSafety_LatchFault(reason);",
        "AxisCommandPort_RejectActive(AXIS_CMD_FAULT);", "command_permit = 0U;",
        "OpenFOC_ApplyCommand(NULL);", "if (capture_adc_fault != 0U)")]
    assert positions == sorted(positions)
    assert re.search(r"capture_adc_fault\s*=.*?board_fault_mask == 0U.*?command_permit != 0U.*?sample_ok == 0U", gate, re.S)
    assert source.index("CurrentMonitor_Run_20kHz(foc_theta);") < fault_start
    safety = (repo / "Core/Src/board_safety.c").read_text(encoding="utf-8")
    latch = safety.split("void BoardSafety_LatchFault(uint32_t reason)", 1)[1].split("\n}", 1)[0]
    assert latch.index("BoardSafety_DisableOutputs();") < latch.index("board_fault_mask |= reason;")
    monitor = (repo / "Core/Src/current_monitor.c").read_text(encoding="utf-8")
    assert re.search(r"if\s*\(CurrentSense_HasNewSample\(\) == 0U\)\s*\{\s*return;\s*\}\s*CurrentSense_GetRaw\(", monitor)
    return "PASS: source-order checks: ADC miss returns before JDR; capture follows original fault/disarm. Full IRQ execution and physical disable timing are NOT modeled.\n"


TASK_WAIT_HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include "cmsis_os2.h"
#include "app_policy.h"
#include "board_safety.h"

/* Model only the external clock/OS. The scheduling algorithm is extracted
 * unchanged below, including the actual application policy constants. */
static unsigned assertions, tick_reads, until_calls, delay_calls, blocked_calls;
static uint32_t tick, deadline, catchup_used, last_until, last_delay, blocked_ticks;
static uint32_t until_race_ticks, wake_lateness, fatal_reason;
static unsigned until_ok_without_block, fatal_calls, expect_fatal;
static osStatus_t until_error, delay_error;
static jmp_buf fatal_jump, task_jump;
static unsigned background_calls, task_start_calls;
static uint32_t body_ticks[3], body_cycles[3];
static struct { uint32_t CYCCNT; } task_dwt;
#define DWT (&task_dwt)
volatile uint32_t app_task_last_cycles, app_task_max_cycles;
static void AppRuntime_TaskStart(void) { task_start_calls++; }
static void AppRuntime_BackgroundStep(void)
{
    if (background_calls == 3U) longjmp(task_jump, 1);
    tick += body_ticks[background_calls];
    task_dwt.CYCCNT += body_cycles[background_calls];
    background_calls++;
    tick_reads = 0U;
}
volatile uint32_t app_task_deadline_misses;
#define CHECK(x) do { assertions++; if (!(x)) { \
 fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
uint32_t osKernelGetTickCount(void)
{
    CHECK(++tick_reads <= 4U); /* A bounded helper must not poll/spin on the clock. */
    return tick;
}
osStatus_t osDelayUntil(uint32_t target)
{
    uint32_t remaining;
    until_calls++; last_until = target;
    if (until_error != osOK) return until_error;
    tick += until_race_ticks;
    remaining = target - tick;
    /* Match this project's CMSIS check, including 32-bit tick wrap. */
    if (remaining == 0U || (remaining >> 31) != 0U) return osErrorParameter;
    if (until_ok_without_block != 0U) {
        /* Tick reaches the target after the CMSIS check but before the inner
         * FreeRTOS delay: CMSIS may still return OK without blocking. */
        tick = target;
        return osOK;
    }
    blocked_calls++; blocked_ticks += remaining;
    tick = target + wake_lateness;
    return osOK;
}
osStatus_t osDelay(uint32_t ticks)
{
    delay_calls++; last_delay = ticks;
    if (delay_error != osOK) return delay_error;
    CHECK(ticks >= 1U);
    blocked_calls++; blocked_ticks += ticks;
    tick += ticks + wake_lateness;
    return osOK;
}
void BoardSafety_Fatal(uint32_t reason)
{
    fatal_calls++; fatal_reason = reason;
    CHECK(expect_fatal != 0U);
    longjmp(fatal_jump, 1);
}

#if AXIS_COMMAND_BENCH_ENABLE
#include "runtime_task_wait_under_test.h"
#endif
#include "runtime_start_task_under_test.h"

static void Reset(uint32_t now, uint32_t due, uint32_t used)
{
    tick = now; deadline = due; catchup_used = used;
    tick_reads = until_calls = delay_calls = blocked_calls = 0U;
    last_until = last_delay = blocked_ticks = 0U;
    until_race_ticks = wake_lateness = fatal_reason = 0U;
    until_ok_without_block = fatal_calls = expect_fatal = 0U;
    until_error = delay_error = osOK;
    app_task_deadline_misses = 0U;
}
#if AXIS_COMMAND_BENCH_ENABLE
static void Wait(void)
{
    unsigned before_until = until_calls, before_delay = delay_calls;
    tick_reads = 0U;
    AppTaskWaitNext(&deadline, &catchup_used);
    CHECK(until_calls - before_until <= 1U);
    CHECK(delay_calls - before_delay <= 1U);
    CHECK((until_calls - before_until) + (delay_calls - before_delay) <= 1U);
    CHECK(fatal_calls == 0U);
}
static void ExpectFatal(void)
{
    uint32_t saved_deadline = deadline, saved_used = catchup_used;
    expect_fatal = 1U; tick_reads = 0U;
    if (setjmp(fatal_jump) == 0) {
        AppTaskWaitNext(&deadline, &catchup_used);
        CHECK(0); /* A scheduler failure cannot return as successful waiting. */
    }
    CHECK(fatal_calls == 1U && fatal_reason == BOARD_FAULT_SCHEDULER);
    CHECK(deadline == saved_deadline && catchup_used == saved_used);
    CHECK(blocked_calls == 0U);
}
#endif
static void CheckStartTaskIntegration(void)
{
    Reset(100U, 0U, 0U);
    background_calls = task_start_calls = 0U;
    task_dwt.CYCCNT = app_task_last_cycles = app_task_max_cycles = 0U;
    body_ticks[0] = 2U; body_ticks[1] = body_ticks[2] = 0U;
    body_cycles[0] = 20U; body_cycles[1] = 40U; body_cycles[2] = 10U;
    if (setjmp(task_jump) == 0) StartAppTask(NULL);
    CHECK(task_start_calls == 1U && background_calls == 3U);
    CHECK(app_task_last_cycles == 10U && app_task_max_cycles == 40U);
    CHECK(app_task_deadline_misses == 1U && fatal_calls == 0U);
#if AXIS_COMMAND_BENCH_ENABLE
    /* Miss -> one immediate body -> forced relative wait even when early ->
     * ordinary absolute wait. The local allowance survives the loop boundary. */
    CHECK(tick == 104U && until_calls == 1U && delay_calls == 1U);
    CHECK(blocked_calls == 2U && blocked_ticks == 2U);
#else
    /* The ordinary target retains its original postpone-and-wait behavior. */
    CHECK(tick == 105U && until_calls == 3U && delay_calls == 0U);
    CHECK(blocked_calls == 3U && blocked_ticks == 3U);
#endif
}
int main(void)
{
#if AXIS_COMMAND_BENCH_ENABLE
    static const osStatus_t other_errors[] = {
        osError, osErrorTimeout, osErrorResource, osErrorNoMemory, osErrorISR
    };
    unsigned i;
    CHECK(APP_PERIOD_TICKS == 1U);
    Reset(100U, 101U, 0U);
    for (i = 0U; i < 20U; i++) {
        Wait();
        CHECK(tick == 101U + i && deadline == tick + 1U && catchup_used == 0U);
        CHECK(last_until == tick && until_calls == i + 1U && delay_calls == 0U);
        CHECK(blocked_calls == i + 1U && blocked_ticks == i + 1U);
        CHECK(app_task_deadline_misses == 0U);
    }
    /* Reaching the deadline exactly and passing it both allow one explicit
     * immediate catch-up; there is no extra tick wait before that iteration. */
    for (i = 0U; i < 3U; i++) {
        Reset(100U + i, 100U, 0U); Wait();
        CHECK(catchup_used == 1U && deadline == tick + 1U);
        CHECK(until_calls == 0U && delay_calls == 0U && blocked_calls == 0U);
        CHECK(app_task_deadline_misses == 1U);
        /* The catch-up finishes early, but a real relative delay is still mandatory. */
        until_ok_without_block = 1U;
        Wait();
        CHECK(delay_calls == 1U && last_delay == 1U && until_calls == 0U);
        CHECK(blocked_calls == 1U && blocked_ticks == 1U && catchup_used == 0U);
        CHECK(deadline == tick + 1U && app_task_deadline_misses == 1U);
        tick = deadline; Wait(); CHECK(catchup_used == 1U);
        CHECK(app_task_deadline_misses == 2U); /* Qualification restored after blocking. */
    }
    Reset(100U, 101U, 0U);
    for (i = 0U; i < 20U; i++) {
        unsigned before_blocks = blocked_calls;
        tick += 2U; /* Model sustained background work exceeding the 1 ms budget. */
        Wait();
        CHECK(catchup_used == ((i & 1U) == 0U ? 1U : 0U));
        CHECK(blocked_calls - before_blocks == (i & 1U));
        CHECK(delay_calls == (i + 1U) / 2U && until_calls == 0U);
        CHECK(app_task_deadline_misses == i + 1U && deadline == tick + 1U);
    }
    /* Rebase after actual wake, not before sleeping or on the obsolete deadline. */
    Reset(130U, 101U, 1U); wake_lateness = 3U; Wait();
    CHECK(tick == 134U && deadline == 135U && catchup_used == 0U);
    CHECK(app_task_deadline_misses == 1U && blocked_ticks == 1U);

    Reset(UINT32_MAX, 0U, 0U); Wait();
    CHECK(tick == 0U && deadline == 1U && blocked_ticks == 1U);
    CHECK(app_task_deadline_misses == 0U && catchup_used == 0U);
    Reset(UINT32_MAX, UINT32_MAX, 0U); Wait();
    CHECK(deadline == 0U && catchup_used == 1U && blocked_calls == 0U);
    Wait(); CHECK(tick == 0U && deadline == 1U && catchup_used == 0U);
    Reset(2U, UINT32_MAX, 0U); Wait();
    CHECK(deadline == 3U && catchup_used == 1U && app_task_deadline_misses == 1U);

    /* Tick expires between the helper's check and CMSIS validation. This
     * consumes the same catch-up allowance, not a second independent retry. */
    Reset(100U, 101U, 0U); until_race_ticks = 1U; Wait();
    CHECK(tick == 101U && deadline == 102U && catchup_used == 1U);
    CHECK(until_calls == 1U && delay_calls == 0U && blocked_calls == 0U);
    CHECK(app_task_deadline_misses == 1U);
    Wait(); CHECK(until_calls == 1U && delay_calls == 1U && blocked_calls == 1U);
    CHECK(tick == 102U && deadline == 103U && catchup_used == 0U);
    Wait(); CHECK(catchup_used == 1U && app_task_deadline_misses == 2U);
    Wait(); CHECK(until_calls == 2U && delay_calls == 2U && blocked_calls == 2U);

    Reset(UINT32_MAX, 0U, 0U); until_race_ticks = 1U; Wait();
    CHECK(tick == 0U && deadline == 1U && catchup_used == 1U);
    Wait(); CHECK(tick == 1U && deadline == 2U && blocked_calls == 1U);

    /* A plain Until-OK is explicitly NOT evidence of physical blocking.
     * Only the used-allowance path must bypass it and call positive osDelay. */
    Reset(100U, 101U, 0U); until_ok_without_block = 1U; Wait();
    CHECK(tick == 101U && deadline == 102U && blocked_calls == 0U);
    CHECK(catchup_used == 0U && app_task_deadline_misses == 0U);
    tick = deadline; Wait(); CHECK(catchup_used == 1U);
    Wait(); CHECK(until_calls == 1U && delay_calls == 1U && blocked_calls == 1U);
    CHECK(catchup_used == 0U && deadline == tick + 1U);

    for (i = 0U; i < sizeof(other_errors) / sizeof(other_errors[0]); i++) {
        Reset(100U, 101U, 0U); until_error = other_errors[i]; ExpectFatal();
        CHECK(until_calls == 1U && delay_calls == 0U);
        Reset(100U, 101U, 1U); delay_error = other_errors[i]; ExpectFatal();
        CHECK(until_calls == 0U && delay_calls == 1U);
    }
    Reset(100U, 101U, 1U); delay_error = osErrorParameter; ExpectFatal();
    CHECK(until_calls == 0U && delay_calls == 1U);
#endif
    CheckStartTaskIntegration();
#if AXIS_COMMAND_BENCH_ENABLE
    printf("PASS: %u bounded appTask waiting assertions (actual helper and StartAppTask)\n", assertions);
#else
    printf("PASS: %u normal appTask waiting assertions (actual unchanged StartAppTask branch)\n", assertions);
#endif
    return 0;
}
"""


ADC_EARLY_HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include "current_monitor.h"
#include "current_sense.h"
#include "Xmath.h"

/* Only hardware ready/timing is a stub. Actual GetRaw reads these JDRs;
 * actual monitor processing calls the real math library through observers. */
typedef struct { volatile uint32_t JDR1, JDR2; } HostAdcRaw;
typedef struct { HostAdcRaw *Instance; } HostRawHandle;
static HostAdcRaw raw_adc[3];
static HostRawHandle raw_handles[3];
static unsigned assertions, checks, reads, raw_accesses, raw_order[8], phase;
static unsigned clarke_calls, sin_calls, park_calls;
static uint8_t hardware_ready;
static float observed_angle, observed_alpha, observed_beta, observed_sine, observed_cosine;
#define CHECK(x) do { assertions++; if (!(x)) { \
 fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static int Near(float a, float b) { return Value_fabsf(a - b) < 0.0001f; }
static HostRawHandle *RawHandle(unsigned adc)
{
    CHECK(raw_accesses < 8U);
    raw_order[raw_accesses++] = adc;
    return &raw_handles[adc - 1U];
}
#define hadc1 (*RawHandle(1U))
#define hadc2 (*RawHandle(2U))
#define hadc3 (*RawHandle(3U))
#define CurrentSense_GetRaw DriverGetRaw
#include "current_raw_read_under_test.h"
#undef CurrentSense_GetRaw
uint8_t CurrentSense_HasNewSample(void)
{
    checks++;
#if AXIS_COMMAND_BENCH_ENABLE
    CHECK(phase == 1U);
#else
    CHECK(phase == 3U);
#endif
    return hardware_ready;
}
void CurrentSense_GetRaw(uint16_t *u, uint16_t *v, uint16_t *w, uint16_t *bus)
{
    reads++;
#if AXIS_COMMAND_BENCH_ENABLE
    CHECK(phase == 1U);
#else
    CHECK(phase == 3U);
#endif
    DriverGetRaw(u, v, w, bus);
}
static void ObserveClarke(float *alpha, float *beta, float ia, float ib)
{
    CHECK(phase == 3U); clarke_calls++;
    clarke(alpha, beta, ia, ib);
    observed_alpha = *alpha; observed_beta = *beta;
}
static void ObserveSinCos(float angle, float *sine, float *cosine)
{
    CHECK(phase == 3U); sin_calls++; observed_angle = angle;
    fast_sin_cos(angle, sine, cosine);
    observed_sine = *sine; observed_cosine = *cosine;
}
static void ObservePark(float *id, float *iq, float alpha, float beta, float sine, float cosine)
{
    CHECK(phase == 3U); park_calls++;
    CHECK(alpha == observed_alpha && beta == observed_beta);
    CHECK(sine == observed_sine && cosine == observed_cosine);
    park(id, iq, alpha, beta, sine, cosine);
}
#define clarke ObserveClarke
#define fast_sin_cos ObserveSinCos
#define park ObservePark
#include "current_monitor_process_under_test.h"
#undef clarke
#undef fast_sin_cos
#undef park

static void SetHardware(const uint16_t values[4])
{
    raw_adc[0].JDR1 = values[0]; raw_adc[1].JDR1 = values[1];
    raw_adc[2].JDR1 = values[2]; raw_adc[2].JDR2 = values[3];
}
static uint8_t Iteration(uint8_t early_ready, uint8_t late_ready,
                         const uint16_t first[4], const uint16_t later[4],
                         float early_angle, float late_angle)
{
#if AXIS_COMMAND_BENCH_ENABLE
    uint16_t raw_u = 0xEEEEU, raw_v = 0xEEEEU, raw_w = 0xEEEEU, raw_bus = 0xEEEEU;
    uint8_t raw_valid;
#endif
    uint32_t before;
    uint8_t sample_ok;
    float foc_theta = early_angle;
    checks = reads = raw_accesses = 0U;
    SetHardware(first); hardware_ready = early_ready; phase = 1U;
#if AXIS_COMMAND_BENCH_ENABLE
#include "runtime_adc_early_under_test.h"
#endif
    /* Simulate intervening rotor/command work and another hardware conversion.
     * These are external changes, not a substitute for the monitor algorithm. */
    SetHardware(later); hardware_ready = late_ready; foc_theta = late_angle;
    phase = 3U;
#include "runtime_adc_process_under_test.h"
    return sample_ok;
}
static void CheckReadOnce(void)
{
    CHECK(checks == 1U && reads == 1U && raw_accesses == 4U);
    CHECK(raw_order[0] == 1U && raw_order[1] == 3U && raw_order[2] == 2U && raw_order[3] == 3U);
}
static void CheckMissingDoesNotAdvance(void)
{
    static const uint16_t first[4] = {111U, 222U, 333U, 444U};
    static const uint16_t later[4] = {3111U, 3222U, 3333U, 3444U};
    uint32_t saved_sequence = monitor_sample_sequence, saved_settle = settle_count, saved_cal = cal_count;
    uint32_t saved_u = sum_u, saved_v = sum_v, saved_w = sum_w;
    uint8_t saved_ready = CurrentMonitor_IsReady();
    float saved_bus = monitor_vbus_v, saved_id = monitor_id_a, saved_iq = monitor_iq_a;
    unsigned saved_park_calls = park_calls;
#if AXIS_COMMAND_BENCH_ENABLE
    /* Ready arriving later must not trigger a second check or reuse last frame. */
    CHECK(Iteration(0U, 1U, first, later, 0.0f, 0.8f) == 0U);
#else
    CHECK(Iteration(1U, 0U, first, later, 0.0f, 0.8f) == 0U);
#endif
    CHECK(checks == 1U && reads == 0U && raw_accesses == 0U);
    CHECK(monitor_sample_sequence == saved_sequence && settle_count == saved_settle && cal_count == saved_cal);
    CHECK(sum_u == saved_u && sum_v == saved_v && sum_w == saved_w);
    CHECK(CurrentMonitor_IsReady() == saved_ready && monitor_vbus_v == saved_bus);
    CHECK(monitor_id_a == saved_id && monitor_iq_a == saved_iq && park_calls == saved_park_calls);
}
static void Calibrate(void)
{
    static const uint16_t baseline[4] = {1000U, 2000U, 3000U, 400U};
#if AXIS_COMMAND_BENCH_ENABLE
    static const uint16_t later[4] = {4011U, 4022U, 4033U, 4044U};
#else
    static const uint16_t later[4] = {1000U, 2000U, 3000U, 400U};
#endif
    uint32_t i;
    CHECK(CURRENT_MONITOR_SETTLE_SAMPLES == 2000U && CURRENT_MONITOR_CAL_SAMPLES == 4096U);
    for (i = 1U; i <= 2000U; i++) {
        CHECK(Iteration(1U, 1U, baseline, later, 0.1f, 0.9f) == 1U);
        CheckReadOnce();
        CHECK(monitor_sample_sequence == i && settle_count == i && cal_count == 0U);
        CHECK(sum_u == 0U && sum_v == 0U && sum_w == 0U && CurrentMonitor_IsReady() == 0U);
        if (i == 1000U) CheckMissingDoesNotAdvance();
    }
    for (i = 1U; i <= 4096U; i++) {
        CHECK(Iteration(1U, 1U, baseline, later, 0.1f, 0.9f) == 1U);
        CheckReadOnce();
        CHECK(monitor_sample_sequence == 2000U + i && settle_count == 2000U && cal_count == i);
        CHECK(sum_u == 1000U * i && sum_v == 2000U * i && sum_w == 3000U * i);
        CHECK(CurrentMonitor_IsReady() == (uint8_t)(i == 4096U));
        if (i == 2048U) CheckMissingDoesNotAdvance();
    }
    CHECK(offset_u == 1000.0f && offset_v == 2000.0f && offset_w == 3000.0f);
    CHECK(clarke_calls == 0U && sin_calls == 0U && park_calls == 0U);
    CHECK(Near(monitor_vbus_v, 400.0f * 3.3f * 301.0f / 4096.0f));
}
int main(void)
{
    static const uint16_t sample[4] = {1100U, 2010U, 2980U, 800U};
    static const uint16_t changed[4] = {4000U, 3990U, 3980U, 2000U};
    unsigned i;
    for (i = 0U; i < 3U; i++) raw_handles[i].Instance = &raw_adc[i];
    CurrentMonitor_Init();
    CHECK(CurrentMonitor_IsReady() == 0U && monitor_sample_sequence == 0U);
    CheckMissingDoesNotAdvance();
    Calibrate();
#if AXIS_COMMAND_BENCH_ENABLE
    CHECK(Iteration(1U, 1U, sample, changed, 0.25f, Value_PI / 2.0f) == 1U);
#else
    CHECK(Iteration(1U, 1U, changed, sample, 0.25f, Value_PI / 2.0f) == 1U);
#endif
    CheckReadOnce();
    CHECK(monitor_sample_sequence == 6097U && CurrentMonitor_IsReady() == 1U);
    /* Independent physical values: one ADC code is 0.030517578125 A at the
     * existing board scaling. U diagnostic differs from reconstructed U. */
    CHECK(Near(monitor_ia_adc_a, -3.0517578125f));
    CHECK(Near(monitor_ib_a, -0.30517578125f) && Near(monitor_ic_a, 0.6103515625f));
    CHECK(Near(monitor_ia_a, -0.30517578125f));
    CHECK(Near(monitor_vbus_v, 800.0f * 3.3f * 301.0f / 4096.0f));
    CHECK(clarke_calls == 1U && sin_calls == 1U && park_calls == 1U);
    CHECK(Near(observed_angle, Value_PI / 2.0f)); /* Tolerate host x87 expression precision. */
    CHECK(Near(monitor_id_a, monitor_i_beta_a) && Near(monitor_iq_a, -monitor_i_alpha_a));
    CheckMissingDoesNotAdvance();
    CHECK(Iteration(1U, 1U, sample, sample, Value_PI / 2.0f, 0.0f) == 1U);
    CheckReadOnce(); CHECK(observed_angle == 0.0f && park_calls == 2U);
    CHECK(Near(monitor_id_a, monitor_i_alpha_a) && Near(monitor_iq_a, monitor_i_beta_a));
    monitor_sample_sequence = UINT32_MAX;
    CHECK(Iteration(1U, 1U, sample, sample, 0.4f, 0.7f) == 1U);
    CHECK(monitor_sample_sequence == 0U); /* Existing != before rule remains wrap-safe. */
    CheckMissingDoesNotAdvance();
#if AXIS_COMMAND_BENCH_ENABLE
    printf("PASS: %u Bench early-ADC processing assertions (actual runtime/monitor/GetRaw and math)\n", assertions);
#else
    printf("PASS: %u normal late-ADC processing assertions (actual runtime/monitor/GetRaw and math)\n", assertions);
#endif
    return 0;
}
"""


AGE_TRACE_HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ad2s1210.h"
#include "app_runtime.h"

static unsigned assertions;
#define CHECK(x) do { assertions++; if (!(x)) { \
 fprintf(stderr, "FAIL feedback timing line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static struct { uint32_t CTRL, CYCCNT; } host_dwt;
#define DWT (&host_dwt)
#define DWT_CTRL_CYCCNTENA_Msk 1U
uint32_t SystemCoreClock = 200000000U;
volatile uint8_t ad2s_dwt_ready = 1U;
volatile uint32_t ad2s_snapshot_max_cycles;
static volatile AD2S1210_FeedbackSnapshot ad2s_snapshot;
static uint32_t host_ms, host_mask, masked_barriers, payload_complete;
static const AD2S1210_FeedbackSnapshot *interrupt_source;
static uint32_t interrupt_check;
static uint32_t HAL_GetTick(void) { CHECK(host_mask == 0U); return host_ms; }
static unsigned protected_copies;
static uint32_t __get_PRIMASK(void) { ++protected_copies; return host_mask; }
static void __disable_irq(void) { host_mask = 1U; }
static void __DMB(void)
{
    if (host_mask != 0U && ++masked_barriers == 2U)
        host_dwt.CYCCNT = payload_complete;
}
static void __set_PRIMASK(uint32_t saved)
{
    host_mask = saved;
    host_dwt.CYCCNT += 5000U; /* Time after unmask must not inflate publish interval. */
    if (saved == 0U && interrupt_source != NULL) {
        AD2S1210_FeedbackSnapshot observed = ad2s_snapshot;
        CHECK(observed.sequence == interrupt_source->sequence);
        CHECK(observed.sample_cycles == interrupt_source->sample_cycles);
        CHECK(observed.valid && observed.speed_valid);
        host_dwt.CYCCNT = interrupt_check;
        AD2S1210_ArmAgeTrace(&observed, interrupt_check);
        interrupt_source = NULL;
    }
}

#include "ad2s_age_under_test.h"
#include "ad2s_publish_under_test.h"
#include "ad2s_get_snapshot_under_test.h"
#include "app_interval_under_test.h"
static AD2S1210_AgeTrace boot_trace;
static AppTaskIntervalTrace boot_interval;

/* Invoke the actual post-acquisition branch with the task's saved markers.
   Only GetSnapshot enters a protected copy during this branch. */
static unsigned CheckStep(uint32_t previous_work_ready, uint32_t previous_body_end_cycles,
                          uint32_t entry_cycles)
{
    unsigned before = protected_copies;
#include "app_interval_check_under_test.h"
    return protected_copies - before;
}

static AD2S1210_FeedbackSnapshot Frame(uint32_t seq, uint32_t edge, uint32_t tick)
{
    AD2S1210_FeedbackSnapshot f;
    memset(&f, 0, sizeof f);
    f.sequence = seq; f.sample_cycles = edge; f.sample_tick_ms = tick;
    f.valid = 1U; f.speed_valid = 1U;
    return f;
}
static void FreshBoot(void)
{
    ad2s_age_trace = boot_trace;
    ad2s_dwt_ready = 1U; host_dwt.CTRL = DWT_CTRL_CYCCNTENA_Msk;
    SystemCoreClock = 200000000U;
    host_mask = 0U; host_ms = 102U; interrupt_source = NULL;
    CHECK(ad2s_age_trace.state == AD2S_AGE_IDLE);
    CHECK(ad2s_age_trace.check_age_cycles == UINT32_MAX);
}
static void Arm(const AD2S1210_FeedbackSnapshot *s, uint32_t check)
{
    host_dwt.CYCCNT = check;
    AD2S1210_ArmAgeTrace(s, check);
}
static void Publish(const AD2S1210_FeedbackSnapshot *f, uint32_t cycles)
{
    masked_barriers = 0U; payload_complete = cycles;
    host_dwt.CYCCNT = cycles - 20U;
    AD2S1210_PublishSnapshot(f);
    CHECK(masked_barriers == 2U);
    CHECK(host_mask == 0U);
}
static void MissingDurations(void)
{
    CHECK(ad2s_age_trace.edge_interval_cycles == UINT32_MAX);
    CHECK(ad2s_age_trace.edge_to_publish_cycles == UINT32_MAX);
}
static void CheckFrozen(const AD2S1210_AgeTrace *frozen)
{
    AD2S1210_AgeTrace observed = ad2s_age_trace;
    CHECK(memcmp(&observed, frozen, sizeof observed) == 0);
}
static void CheckEdge(uint32_t seq, uint32_t start, uint32_t source_tick,
                      uint32_t successor_delta, uint32_t expected)
{
    AD2S1210_FeedbackSnapshot a = Frame(seq, start, source_tick);
    AD2S1210_FeedbackSnapshot b = Frame(seq + 1U, start + successor_delta, source_tick + 2U);
    AD2S1210_AgeTrace saved;
    uint32_t check = start + 401056U, publication = start + 501056U;
    FreshBoot(); host_ms = source_tick + 2U;
    Arm(&a, check);
    CHECK(ad2s_age_trace.state == AD2S_AGE_PENDING);
    CHECK(ad2s_age_trace.source_sequence == seq && ad2s_age_trace.check_age_cycles == 401056U);
    MissingDurations();
    saved = ad2s_age_trace;
    Arm(&b, check + 100U); CheckFrozen(&saved);
    host_ms = source_tick + 3U;
    Publish(&b, publication);
    CHECK(ad2s_age_trace.state == expected);
    CHECK(ad2s_age_trace.next_sequence == b.sequence);
    CHECK(ad2s_age_trace.edge_interval_cycles == successor_delta);
    CHECK(ad2s_age_trace.edge_to_publish_cycles == (uint32_t)(501056U - successor_delta));
    CHECK(ad2s_age_trace.check_age_cycles == 401056U);
    saved = ad2s_age_trace;
    b.sequence++; b.sample_cycles += 200000U; b.sample_tick_ms++;
    Publish(&b, publication + 200000U); Arm(&b, check + 400000U); CheckFrozen(&saved);
}
static void CheckOldPublishInterrupted(void)
{
    AD2S1210_FeedbackSnapshot a = Frame(17U, 10000U, 100U);
    AD2S1210_FeedbackSnapshot b = Frame(18U, 420000U, 102U);
    FreshBoot(); interrupt_source = &a; interrupt_check = 411056U;
    Publish(&a, 410000U);
    CHECK(interrupt_source == NULL && ad2s_age_trace.state == AD2S_AGE_PENDING);
    MissingDurations();
    Publish(&b, 441056U);
    CHECK(ad2s_age_trace.state == AD2S_AGE_EDGE_AFTER_CHECK);
    CHECK(ad2s_age_trace.edge_interval_cycles == 410000U);
    CHECK(ad2s_age_trace.edge_to_publish_cycles == 21056U);
}
static void CheckUnusable(void)
{
    AD2S1210_FeedbackSnapshot a = Frame(7U, 10000U, 100U);
    AD2S1210_FeedbackSnapshot b = Frame(8U, 400000U, 102U);
    AD2S1210_AgeTrace frozen;
    unsigned mode;
    for (mode = 0U; mode < 10U; ++mode) {
        FreshBoot(); Arm(&a, 411056U);
        b = Frame(8U, 400000U, 102U);
        if (mode == 0U) { b.valid = b.speed_valid = 0U; b.sample_cycles = a.sample_cycles; }
        if (mode == 1U) b.sequence = 9U;
        if (mode == 2U) b.sample_cycles = 450000U; /* Edge after publication. */
        if (mode == 3U) host_ms += 1100U; /* Long DWT ambiguity. */
        if (mode == 4U) ad2s_dwt_ready = 0U;
        if (mode == 5U) b.sample_tick_ms = 99U; /* Edge tick predates source. */
        if (mode == 6U) b.sample_cycles = a.sample_cycles; /* Zero interval. */
        if (mode == 7U) host_dwt.CTRL = 0U;
        Publish(&b, mode == 8U ? 411056U : (mode == 9U ? a.sample_cycles + 0x80000000U : 440000U));
        CHECK(ad2s_age_trace.state == (mode == 0U ? AD2S_AGE_INVALID_NEXT : AD2S_AGE_UNKNOWN));
        MissingDurations();
        frozen = ad2s_age_trace;
        b = Frame(9U, 455000U, 103U); host_ms = 103U;
        Publish(&b, 460000U); CheckFrozen(&frozen);
    }
    FreshBoot(); Arm(NULL, 123U);
    CHECK(ad2s_age_trace.state == AD2S_AGE_UNKNOWN); MissingDurations();
    FreshBoot(); a.valid = 0U;
    Arm(&a, 411056U); CHECK(ad2s_age_trace.state == AD2S_AGE_UNKNOWN); MissingDurations();
    FreshBoot(); a.valid = 1U; a.speed_valid = 0U;
    Arm(&a, 411056U); CHECK(ad2s_age_trace.state == AD2S_AGE_UNKNOWN); MissingDurations();
}
static void CheckNoPartition(uint32_t state)
{
    CHECK(app_task_interval_trace.state == state);
    CHECK(app_task_interval_trace.old_edge_to_end_cycles == UINT32_MAX);
    CHECK(app_task_interval_trace.between_steps_cycles == UINT32_MAX);
    CHECK(app_task_interval_trace.entry_to_edge_cycles == UINT32_MAX);
    CHECK(app_task_interval_trace.old_edge_to_acquisition_end_cycles == UINT32_MAX);
    CHECK(app_task_interval_trace.old_telemetry_queue_cycles == UINT32_MAX);
}

/* The source's publication may resume after the ISR has armed the age trace.
   Its task end and the next entry become available before the successor returns. */
static void BeginPartition(uint32_t seq, uint32_t start, uint32_t span, unsigned interrupt_source_publish)
{
    AD2S1210_FeedbackSnapshot a = Frame(seq, start, 100U);
    AD2S1210_FeedbackSnapshot b = Frame(seq + 1U, start + span, 102U);
    FreshBoot(); app_task_interval_trace = boot_interval;
    CHECK(CheckStep(1U, start, start) == 0U);
    CheckNoPartition(APP_TASK_INTERVAL_WAITING);
    if (interrupt_source_publish != 0U) {
        interrupt_source = &a; interrupt_check = start + 401056U;
        Publish(&a, start + 401000U);
        CHECK(ad2s_age_trace.state == AD2S_AGE_PENDING);
    } else {
        Publish(&a, start + 20000U);
        CHECK(ad2s_age_trace.state == AD2S_AGE_IDLE);
    }
    CHECK(CheckStep(1U, start - 200000U, start - 10000U) == 0U);
    CheckNoPartition(APP_TASK_INTERVAL_WAITING);
    if (interrupt_source_publish == 0U) {
        AD2S1210_FeedbackSnapshot observed;
        AD2S1210_GetSnapshot(&observed);
        Arm(&observed, start + 401056U); /* ISR during the successor's acquisition. */
    }
    Publish(&b, start + span + 49722U);
    CHECK(ad2s_age_trace.edge_interval_cycles == span);
    CHECK(ad2s_age_trace.edge_to_publish_cycles == 49722U);
}

static void CheckPartition(uint32_t seq, uint32_t start, uint32_t span,
                           uint32_t tail, uint32_t between, uint32_t lead,
                           uint32_t age_state, unsigned interrupt_source_publish)
{
    AD2S1210_FeedbackSnapshot later = Frame(seq + 2U, start + span + 200000U, 103U);
    BeginPartition(seq, start, span, interrupt_source_publish);
    CHECK(ad2s_age_trace.state == age_state);
    CHECK(ad2s_age_trace.source_sequence == seq && ad2s_age_trace.next_sequence == seq + 1U);
    previous_work_timing.acquisition_end_cycles = start + tail / 2U;
    previous_work_timing.telemetry_cycles = tail / 8U;
    CHECK(CheckStep(1U, start + tail, start + tail + between) == 1U);
    previous_work_timing.acquisition_end_cycles = later.sample_cycles;
    previous_work_timing.telemetry_cycles = 1U;
    Publish(&later, later.sample_cycles + 30000U);
    CHECK(CheckStep(1U, later.sample_cycles, later.sample_cycles) == 0U);
    CHECK(app_task_interval_trace.state == APP_TASK_INTERVAL_COMPLETE);
    CHECK(app_task_interval_trace.old_edge_to_end_cycles == tail);
    CHECK(app_task_interval_trace.between_steps_cycles == between);
    CHECK(app_task_interval_trace.entry_to_edge_cycles == lead);
    CHECK(app_task_interval_trace.old_edge_to_acquisition_end_cycles == tail / 2U);
    CHECK(app_task_interval_trace.old_telemetry_queue_cycles == tail / 8U);
    CHECK(tail + between + lead == ad2s_age_trace.edge_interval_cycles);
}

static void CheckTailDetails(void)
{
    const uint32_t start = UINT32_MAX - 999U;
    const uint32_t span = 399840U;
    const uint32_t tail = 230197U;
    unsigned i;
    for (i = 0U; i < 5U; ++i) {
        BeginPartition(24U, start, span, 0U);
        previous_work_timing.acquisition_end_cycles = start + 38000U;
        previous_work_timing.telemetry_cycles = 0U; /* No telemetry due in this step. */
        if (i == 1U) previous_work_timing.acquisition_end_cycles = start - 1U;
        if (i == 2U) previous_work_timing.acquisition_end_cycles = start + tail + 1U;
        if (i == 3U) previous_work_timing.telemetry_cycles = tail - 38000U + 1U;
        if (i == 4U) previous_work_timing.telemetry_cycles = tail - 38000U;
        CHECK(CheckStep(1U, start + tail, start + tail + 149629U) == 1U);
        CHECK(app_task_interval_trace.state == APP_TASK_INTERVAL_COMPLETE);
        CHECK(app_task_interval_trace.old_edge_to_end_cycles == tail);
        CHECK(app_task_interval_trace.entry_to_edge_cycles == 20014U);
        if (i == 0U || i == 4U) {
            CHECK(app_task_interval_trace.old_edge_to_acquisition_end_cycles == 38000U);
            CHECK(app_task_interval_trace.old_telemetry_queue_cycles == previous_work_timing.telemetry_cycles);
        } else {
            CHECK(app_task_interval_trace.old_edge_to_acquisition_end_cycles == UINT32_MAX);
            CHECK(app_task_interval_trace.old_telemetry_queue_cycles == UINT32_MAX);
        }
    }
}

static void CheckUnusablePartition(void)
{
    uint32_t start = 10000U, span = 399758U;
    unsigned i;
    for (i = 0U; i < 12U; ++i) {
        uint32_t end = start + 210000U, entry = start + 380000U, ready = 1U;
        unsigned copies = 1U;
        AD2S1210_FeedbackSnapshot b = Frame(18U, start + span, 102U);
        BeginPartition(17U, start, span, 0U);
        switch (i) {
        case 0: end = start - 1U; break;
        case 1: entry = end - 1U; break;
        case 2: entry = start + span + 1U; break;
        case 3: ready = 0U; copies = 0U; break;
        case 4: /* Another acquisition occurred before the background observation. */
            b.sequence++; b.sample_cycles += 200000U; Publish(&b, b.sample_cycles + 25000U); break;
        case 5: b.valid = 0U; Publish(&b, b.sample_cycles + 25000U); break;
        case 6: ad2s_age_trace.source_sequence--; break;
        case 7: ad2s_age_trace.edge_interval_cycles = 0U; copies = 0U; break;
        case 8: ad2s_age_trace.edge_interval_cycles = 0x80000000U; copies = 0U; break;
        case 9: ad2s_age_trace.edge_interval_cycles = UINT32_MAX; copies = 0U; break;
        case 10: /* First successor is an actual invalid position attempt. */
        case 11: {
            AD2S1210_FeedbackSnapshot a = Frame(17U, start, 100U);
            FreshBoot(); Arm(&a, start + 401056U);
            if (i == 10U) { b.valid = 0U; b.sample_cycles = start; }
            else b.sequence++;
            Publish(&b, start + span + 49722U);
            CHECK(ad2s_age_trace.state == (i == 10U ? AD2S_AGE_INVALID_NEXT : AD2S_AGE_UNKNOWN));
            copies = 0U; break;
        }
        }
        CHECK(CheckStep(ready, end, entry) == copies);
        CheckNoPartition(APP_TASK_INTERVAL_UNAVAILABLE);
        CHECK(CheckStep(1U, start, start + span) == 0U);
        CheckNoPartition(APP_TASK_INTERVAL_UNAVAILABLE);
    }
}

int main(void)
{
    unsigned age_assertions;
    boot_interval = app_task_interval_trace;
    boot_trace = ad2s_age_trace;
    CheckEdge(1U, 10000U, 100U, 205000U, AD2S_AGE_EDGE_BY_CHECK);
    CheckEdge(2U, 10000U, 100U, 401056U, AD2S_AGE_EDGE_BY_CHECK);
    CheckEdge(3U, 10000U, 100U, 401057U, AD2S_AGE_EDGE_AFTER_CHECK);
    CheckEdge(UINT32_MAX, UINT32_MAX - 100000U, UINT32_MAX - 1U,
              390000U, AD2S_AGE_EDGE_BY_CHECK);
    CheckEdge(5U, 0U, 0U, 200000U, AD2S_AGE_EDGE_BY_CHECK);
    CheckOldPublishInterrupted(); CheckUnusable();
    printf("PASS: %u feedback age pairing assertions (production arm/completion and PublishSnapshot)\n", assertions);
    age_assertions = assertions;
    CheckPartition(17U, 10000U, 399758U, 101450U, 285850U, 12458U, AD2S_AGE_EDGE_BY_CHECK, 0U);
    CheckPartition(19U, 10000U, 410000U, 403000U, 4000U, 3000U, AD2S_AGE_EDGE_AFTER_CHECK, 1U);
    CheckPartition(UINT32_MAX, UINT32_MAX - 99999U, 410000U, 100000U, 0U, 310000U,
                   AD2S_AGE_EDGE_AFTER_CHECK, 0U); /* sequence, end and entry wrap to zero */
    /* Exercise inclusive arithmetic boundaries with coincident task and edge markers. */
    CheckPartition(0U, 0U, 399758U, 0U, 399758U, 0U, AD2S_AGE_EDGE_BY_CHECK, 0U);
    CheckUnusablePartition();
    CheckTailDetails();
    printf("PASS: %u task interval assertions (actual background gate, capture, publish and snapshot copy)\n",
           assertions - age_assertions);
    return 0;
}
"""


def check_early_adc_order(source, early_block, process_block):
    fast = source.split("void AppRuntime_FastISR(void)", 1)[1]
    order = [fast.index(text) for text in (
        "BoardSafety_PowerOutputsAreDisabled()", "BoardSafety_DisableOutputs();",
        "/* ADC EARLY READ BEGIN", "AD2S1210_GetSnapshot(&snapshot);",
        "AxisCommandPort_BeginCycle(", "OpenFOC_SetRotorFeedback(", "OpenFOC_Ramp_1ms();",
        "/* ADC RAW PROCESS BEGIN", "if (sample_ok == 0U) reason |= BOARD_FAULT_ADC_FRAME;")]
    assert order == sorted(order)
    assert fast.count("CurrentSense_HasNewSample()") == 1
    assert fast.count("CurrentSense_GetRaw(") == 1
    assert re.search(r"raw_valid = CurrentSense_HasNewSample\(\);\s*if \(raw_valid != 0U\)\s*\{\s*CurrentSense_GetRaw\(", early_block)
    assert re.search(r"if \(raw_valid != 0U\)\s*CurrentMonitor_ProcessRaw_20kHz\(foc_theta, raw_u, raw_v, raw_w, raw_bus\);", process_block)
    assert "sample_ok = (uint8_t)(monitor_sample_sequence != before);" in process_block
    return "PASS: source-order checks: Bench safety gate precedes one early read before resolver/commands; raw processing remains at the original late angle point. Not a complete FastISR or physical same-frame timing model.\n"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=r"D:\FPGA\QuartusII_13.0\quartus\bin64\cygwin\bin\gcc.exe")
    parser.add_argument("--output-dir", default="build/008-can-boot-identity")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    out = repo / args.output_dir
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
    ready_begin = "/* READY LOSS TRACE BEGIN: tested from this production block on the host. */"
    ready_end = "/* READY LOSS TRACE END */"
    assert source.count(ready_begin) == source.count(ready_end) == 1
    ready_block = source.split(ready_begin, 1)[1].split(ready_end, 1)[0]
    (out / "runtime_ready_loss_under_test.h").write_text(ready_block, encoding="utf-8", newline="\n")
    (out / "runtime_ready_loss_host.c").write_text(READY_LOSS_HARNESS, encoding="utf-8", newline="\n")
    ready_exe = out / "runtime_ready_loss_host.exe"
    ready_command = [args.compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
                     "-DAXIS_COMMAND_BENCH_ENABLE=1", "-DBOARD_POWER_STAGE_MODE=0",
                     "-I", str(repo / "Core/Inc"), "-I", str(out),
                     str(out / "runtime_ready_loss_host.c"), "-o", str(ready_exe)]
    log += ["Ready-loss production block SHA256: " + hashlib.sha256(ready_block.encode()).hexdigest() + "\n"]
    for cmd in (ready_command, [str(ready_exe)]):
        proc = subprocess.run(cmd, cwd=repo, env=env, text=True, errors="replace",
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log += ["COMMAND: "+subprocess.list2cmdline(cmd)+"\n", proc.stdout,
                "EXIT: "+str(proc.returncode)+"\n"]
        (out/"runtime-host.txt").write_text("".join(log), encoding="utf-8", newline="\n")
        if proc.returncode:
            raise RuntimeError(proc.stdout)
        print(proc.stdout.strip())
    print("PASS: extracted ready-loss diagnostics; full FastISR/sensor/IRQ timing is not modeled.")


    driver = (repo / "Core/Src/ad2s1210.c").read_text(encoding="utf-8")
    age_begin = "/* AD2S AGE TRACE BEGIN"
    age_end = "/* AD2S AGE TRACE END"
    assert driver.count(age_begin) == driver.count(age_end) == 1
    age_block = driver.split(age_begin, 1)[1].split("*/", 1)[1].split(age_end, 1)[0]
    publish_start = driver.index("static void AD2S1210_PublishSnapshot(")
    getter_start = driver.index("void AD2S1210_GetSnapshot(", publish_start)
    getter_end = driver.index("\n}\n", getter_start) + 3
    publish_block = driver[publish_start:getter_start]
    getter_block = driver[getter_start:getter_end]
    background = source.split("void AppRuntime_BackgroundStep(void)", 1)[1].split("void AppRuntime_FastISR", 1)[0]
    milestones = [background.index(token) for token in (
        "entry_cycles = DWT->CYCCNT", "AD2S1210_Run_1kHz();", "work_timing.acquisition_end_cycles = DWT->CYCCNT",
        "work_timing.acquisition_cycles =",
        "/* TASK INTERVAL CHECK BEGIN */", "/* TASK INTERVAL CHECK END */", "CaptureSampleGap(",
        "previous_work_timing = work_timing;", "previous_work_ready =", "previous_body_end_cycles = DWT->CYCCNT")]
    assert milestones == sorted(milestones)
    interval_blocks = []
    for label, filename in (("TRACE", "app_interval_under_test.h"), ("CHECK", "app_interval_check_under_test.h")):
        begin, end = (f"/* TASK INTERVAL {label} {marker} */" for marker in ("BEGIN", "END"))
        assert source.count(begin) == source.count(end) == 1
        interval_blocks.append((filename, source.split(begin, 1)[1].split(end, 1)[0]))
    for name, content in (("ad2s_age_under_test.h", age_block), ("ad2s_publish_under_test.h", publish_block),
                          ("ad2s_get_snapshot_under_test.h", getter_block), *interval_blocks):
        (out / name).write_text(content, encoding="utf-8", newline="\n")
        log += [name + " production SHA256: " + hashlib.sha256(content.encode()).hexdigest() + "\n"]
    (out / "ad2s_feedback_age_host.c").write_text(AGE_TRACE_HARNESS, encoding="utf-8", newline="\n")
    age_exe = out / "ad2s_feedback_age_host.exe"
    age_command = ready_command.copy()
    age_command[age_command.index(str(out / "runtime_ready_loss_host.c"))] = str(out / "ad2s_feedback_age_host.c")
    age_command[-1] = str(age_exe)
    for cmd in (age_command, [str(age_exe)]):
        proc = subprocess.run(cmd, cwd=repo, env=env, text=True, errors="replace",
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log += ["COMMAND: "+subprocess.list2cmdline(cmd)+"\n", proc.stdout, "EXIT: "+str(proc.returncode)+"\n"]
        (out/"runtime-host.txt").write_text("".join(log), encoding="utf-8", newline="\n")
        if proc.returncode:
            raise RuntimeError(proc.stdout)
        print(proc.stdout.strip())

    adc_source = (repo / "Core/Src/current_sense.c").read_text(encoding="utf-8")
    adc_begin = "/* ADC CHECK TRACE BEGIN: tested from this production block on the host. */"
    adc_end = "/* ADC CHECK TRACE END */"
    assert adc_source.count(adc_begin) == adc_source.count(adc_end) == 1
    adc_block = adc_source.split(adc_begin, 1)[1].split(adc_end, 1)[0]
    (out / "current_adc_phase_under_test.h").write_text(adc_block, encoding="utf-8", newline="\n")
    (out / "current_adc_phase_host.c").write_text(ADC_PHASE_HARNESS, encoding="utf-8", newline="\n")
    adc_exe = out / "current_adc_phase_host.exe"
    adc_command = [args.compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
                   "-DAXIS_COMMAND_BENCH_ENABLE=1", "-DBOARD_POWER_STAGE_MODE=0",
                   "-I", str(repo / "Core/Inc"), "-I", str(out),
                   str(out / "current_adc_phase_host.c"), "-o", str(adc_exe)]
    log += ["ADC phase production block SHA256: " + hashlib.sha256(adc_block.encode()).hexdigest() + "\n"]
    for cmd in (adc_command, [str(adc_exe)]):
        proc = subprocess.run(cmd, cwd=repo, env=env, text=True, errors="replace",
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log += ["COMMAND: "+subprocess.list2cmdline(cmd)+"\n", proc.stdout,
                "EXIT: "+str(proc.returncode)+"\n"]
        (out/"runtime-host.txt").write_text("".join(log), encoding="utf-8", newline="\n")
        if proc.returncode:
            raise RuntimeError(proc.stdout)
        print(proc.stdout.strip())
    order_evidence = check_adc_runtime_order(repo, source)
    log += [order_evidence]
    (out/"runtime-host.txt").write_text("".join(log), encoding="utf-8", newline="\n")
    print(order_evidence.strip())


    task_source = (repo / "Core/Src/freertos.c").read_text(encoding="utf-8")
    task_begin = "/* APP TASK WAIT BEGIN: tested from this production block on the host. */"
    task_end = "/* APP TASK WAIT END */"
    assert task_source.count(task_begin) == task_source.count(task_end) == 1
    task_block = task_source.split(task_begin, 1)[1].split(task_end, 1)[0]
    task_entry = "void StartAppTask(void *argument)\n{"
    assert task_source.count(task_entry) == 1
    task_entry_block = task_entry + task_source.split(task_entry, 1)[1].split("/* Private application code", 1)[0]
    (out / "runtime_start_task_under_test.h").write_text(task_entry_block, encoding="utf-8", newline="\n")
    (out / "runtime_task_wait_under_test.h").write_text(task_block, encoding="utf-8", newline="\n")
    (out / "runtime_task_wait_host.c").write_text(TASK_WAIT_HARNESS, encoding="utf-8", newline="\n")
    task_exe = out / "runtime_task_wait_host.exe"
    task_command = [args.compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-DAXIS_COMMAND_BENCH_ENABLE=1", "-DBOARD_POWER_STAGE_MODE=0",
                    "-I", str(repo / "Core/Inc"), "-I", str(out),
                    "-I", str(repo / "Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2"),
                    str(out / "runtime_task_wait_host.c"), "-o", str(task_exe)]
    log += ["Task-wait production block SHA256: " + hashlib.sha256(task_block.encode()).hexdigest() + "\n"]
    log += ["StartAppTask production block SHA256: " + hashlib.sha256(task_entry_block.encode()).hexdigest() + "\n"]
    for cmd in (task_command, [str(task_exe)]):
        proc = subprocess.run(cmd, cwd=repo, env=env, text=True, errors="replace",
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log += ["COMMAND: "+subprocess.list2cmdline(cmd)+"\n", proc.stdout,
                "EXIT: "+str(proc.returncode)+"\n"]
        (out/"runtime-host.txt").write_text("".join(log), encoding="utf-8", newline="\n")
        if proc.returncode:
            raise RuntimeError(proc.stdout)
        print(proc.stdout.strip())
    normal_exe = out / "runtime_task_wait_normal_host.exe"
    normal_command = [part.replace("-DAXIS_COMMAND_BENCH_ENABLE=1", "-DAXIS_COMMAND_BENCH_ENABLE=0")
                      .replace("-DBOARD_POWER_STAGE_MODE=0", "-DBOARD_POWER_STAGE_MODE=1")
                      for part in task_command]
    normal_command[-1] = str(normal_exe)
    for cmd in (normal_command, [str(normal_exe)]):
        proc = subprocess.run(cmd, cwd=repo, env=env, text=True, errors="replace",
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log += ["COMMAND: "+subprocess.list2cmdline(cmd)+"\n", proc.stdout,
                "EXIT: "+str(proc.returncode)+"\n"]
        (out/"runtime-host.txt").write_text("".join(log), encoding="utf-8", newline="\n")
        if proc.returncode:
            raise RuntimeError(proc.stdout)
        print(proc.stdout.strip())
    task_order = "PASS: extracted Bench scheduling helper; external clock/OS blocking are modeled, not real RTOS timing or proof that every Until-OK physically blocks.\n"
    log += [task_order]
    (out/"runtime-host.txt").write_text("".join(log), encoding="utf-8", newline="\n")
    print(task_order.strip())


    monitor_source = (repo / "Core/Src/current_monitor.c").read_text(encoding="utf-8")
    monitor_begin = "/* CURRENT MONITOR PROCESS BEGIN: tested from this production block on the host. */"
    monitor_end = "/* CURRENT MONITOR PROCESS END */"
    assert monitor_source.count(monitor_begin) == monitor_source.count(monitor_end) == 1
    monitor_block = monitor_source.split(monitor_begin, 1)[1].split(monitor_end, 1)[0]
    early_begin = "/* ADC EARLY READ BEGIN: tested from this production block on the host. */"
    early_end = "/* ADC EARLY READ END */"
    process_begin = "/* ADC RAW PROCESS BEGIN: tested from this production block on the host. */"
    process_end = "/* ADC RAW PROCESS END */"
    assert source.count(early_begin) == source.count(early_end) == 1
    assert source.count(process_begin) == source.count(process_end) == 1
    early_block = source.split(early_begin, 1)[1].split(early_end, 1)[0]
    process_block = source.split(process_begin, 1)[1].split(process_end, 1)[0]
    raw_begin = "void CurrentSense_GetRaw("
    assert adc_source.count(raw_begin) == 1
    raw_block = raw_begin + adc_source.split(raw_begin, 1)[1].split("\n}", 1)[0] + "\n}\n"
    for name, content in (("current_monitor_process_under_test.h", monitor_block),
                          ("runtime_adc_early_under_test.h", early_block),
                          ("runtime_adc_process_under_test.h", process_block),
                          ("current_raw_read_under_test.h", raw_block)):
        (out / name).write_text(content, encoding="utf-8", newline="\n")
        log += [name + " production SHA256: " + hashlib.sha256(content.encode()).hexdigest() + "\n"]
    # Xmath.h includes main.h, but its math implementation uses no HAL symbols.
    # Keep this hardware-header placeholder solely inside the generated build fixture.
    (out / "main.h").write_text("#ifndef HOST_MATH_MAIN_H\n#define HOST_MATH_MAIN_H\n#include <stdint.h>\n#endif\n", encoding="utf-8", newline="\n")
    (out / "runtime_adc_early_host.c").write_text(ADC_EARLY_HARNESS, encoding="utf-8", newline="\n")
    for bench in (1, 0):
        early_exe = out / ("runtime_adc_early_bench.exe" if bench else "runtime_adc_late_normal.exe")
        early_command = [args.compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
                         "-DAXIS_COMMAND_BENCH_ENABLE=" + str(bench),
                         "-DBOARD_POWER_STAGE_MODE=" + str(0 if bench else 1),
                         "-I", str(out), "-I", str(repo / "Core/Inc"),
                         "-I", str(repo / "Core/math"), str(out / "runtime_adc_early_host.c"),
                         str(repo / "Core/math/Xmath.c"), "-o", str(early_exe)]
        for cmd in (early_command, [str(early_exe)]):
            proc = subprocess.run(cmd, cwd=repo, env=env, text=True, errors="replace",
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            log += ["COMMAND: "+subprocess.list2cmdline(cmd)+"\n", proc.stdout,
                    "EXIT: "+str(proc.returncode)+"\n"]
            (out/"runtime-host.txt").write_text("".join(log), encoding="utf-8", newline="\n")
            if proc.returncode:
                raise RuntimeError(proc.stdout)
            print(proc.stdout.strip())
    early_evidence = check_early_adc_order(source, early_block, process_block)
    log += [early_evidence]
    (out/"runtime-host.txt").write_text("".join(log), encoding="utf-8", newline="\n")
    print(early_evidence.strip())

if __name__ == "__main__":
    main()
