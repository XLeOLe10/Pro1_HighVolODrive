/* Host checks for the exact USER CODE 2 idle hook extracted from freertos.c.
 * Stub time/stack API only: this does not simulate the scheduler or IRQ latency. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "app_policy.h"

typedef void *TaskHandle_t;
typedef uint32_t StackType_t;
typedef uint32_t UBaseType_t;

static unsigned checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
    exit(1); } } while (0)

static unsigned app_task_token;
static void *appTaskHandle;
static uint32_t mock_tick;
static uint32_t mock_scan_elapsed;
static UBaseType_t mock_free_words;
static unsigned scan_calls;

#include "probe_state_under_test.h"

static uint32_t HAL_GetTick(void)
{
    return mock_tick;
}

static UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task)
{
    CHECK(task != NULL);
    CHECK(task == (TaskHandle_t)&app_task_token);
    ++scan_calls;
    /* Advance elapsed time as if the low-priority scan had been preempted. */
    mock_tick += mock_scan_elapsed;
    return mock_free_words;
}

#include "idle_hook_under_test.h"

static void expect_no_scan(uint32_t tick)
{
    unsigned before_calls = scan_calls;
    uint32_t before_count = app_stack_probe_count;
    uint32_t before_time = app_stack_probe_last_tick_ms;
    uint32_t before_free = app_stack_min_free_bytes;
    mock_tick = tick;
    vApplicationIdleHook();
    CHECK(scan_calls == before_calls);
    CHECK(app_stack_probe_count == before_count);
    CHECK(app_stack_probe_last_tick_ms == before_time);
    CHECK(app_stack_min_free_bytes == before_free);
}

static void expect_scan(uint32_t tick, uint32_t elapsed,
                        UBaseType_t words, uint32_t expected_min)
{
    unsigned before_calls = scan_calls;
    uint32_t before_count = app_stack_probe_count;
    mock_tick = tick;
    mock_scan_elapsed = elapsed;
    mock_free_words = words;
    vApplicationIdleHook();
    CHECK(scan_calls == before_calls + 1U);
    CHECK(app_stack_probe_count == before_count + 1U);
    CHECK(app_stack_probe_last_tick_ms == (uint32_t)(tick + elapsed));
    CHECK(app_stack_min_free_bytes == expected_min);
}

int main(void)
{
    unsigned i;
    CHECK(APP_DIAGNOSTIC_PERIOD_MS == 1000U);
    CHECK(app_stack_min_free_bytes == UINT32_MAX);
    CHECK(app_stack_probe_count == 0U);
    CHECK(app_stack_probe_last_tick_ms == 0U);
#if AXIS_COMMAND_BENCH_ENABLE
    CHECK(app_stack_probe_enabled == 1U);
#endif

    /* An uncreated appTask must never fall through to NULL (= current Idle). */
    appTaskHandle = NULL;
    expect_no_scan(1000U);
    appTaskHandle = &app_task_token;
    expect_no_scan(0U);
    expect_no_scan(999U);
    expect_scan(1000U, 0U, 700U, 2800U);
    expect_no_scan(1000U);
    expect_no_scan(1999U);

    /* Keep a conservative historic minimum even if a later scan looks larger. */
    expect_scan(2000U, 0U, 800U, 2800U);
    expect_scan(3000U, 0U, 600U, 2400U);
#if AXIS_COMMAND_BENCH_ENABLE
    app_stack_probe_enabled = 0U;
    expect_no_scan(4000U);
    app_stack_probe_enabled = 1U;
#endif
    expect_scan(4000U, 0U, 500U, 2000U);

    /* Schedule from COMPLETION, not start. Long scan must not cause catch-up. */
    expect_scan(5000U, 2200U, 450U, 1800U);
    expect_no_scan(7200U);
    expect_no_scan(8199U);
    expect_scan(8200U, 0U, 440U, 1760U);
    expect_scan(100000U, 0U, 430U, 1720U);
    for (i = 0; i < 8U; ++i) expect_no_scan(100000U);

    /* Both due-time comparison and published time tolerate HAL tick wrap. */
    expect_scan(UINT32_MAX - 500U, 0U, 420U, 1680U);
    expect_no_scan(498U); /* 999 ms since completion, modulo uint32_t */
    expect_scan(499U, 0U, 410U, 1640U);
    expect_scan(1499U, 0U, 0U, 0U);
    expect_scan(2499U, 0U, 900U, 0U);

    printf("PASS: %s idle hook, %u checks, %u actual hook scans\n",
           AXIS_COMMAND_BENCH_ENABLE ? "bench" : "normal", checks, scan_calls);
    return 0;
}
