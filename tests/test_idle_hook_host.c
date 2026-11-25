/* Production idle hook, appTask due-time branch, telemetry handoff and CSV format.
 * Inject task releases at the formatter and UART boundaries. HAL, time and stack
 * are simulated; scheduler/IRQ latency and wire delivery require hardware. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "telemetry_format.h"
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


static unsigned interrupt_mask;
static struct { uint32_t CYCCNT; } cycle_counter;
#define DWT (&cycle_counter)
#define __get_PRIMASK() (interrupt_mask)
#define __disable_irq() ((void)(interrupt_mask = 1U))
#define __set_PRIMASK(saved) ((void)(interrupt_mask = (saved)))
#define __DMB() ((void)0)
static void RecordCriticalCycles(uint32_t start)
{
    (void)start;
    CHECK(interrupt_mask == 1U);
}

typedef struct { unsigned gState; } UART_HandleTypeDef;
enum { HAL_UART_STATE_READY = 1U, HAL_UART_STATE_BUSY_TX, HAL_OK, HAL_ERROR };
static UART_HandleTypeDef huart1;
static unsigned uart_result = HAL_OK;
static unsigned format_calls, uart_calls;
static unsigned interrupt_site;
enum { INSIDE_FORMAT = 1U, INSIDE_UART };
static uint32_t interrupt_tick;
static unsigned queue_on_interrupt;
static void run_background_due(uint32_t now);
static void interrupt_at(unsigned site)
{
    if (interrupt_site != site) return;
    interrupt_site = 0U;
    mock_tick = interrupt_tick;
    if (queue_on_interrupt) run_background_due(mock_tick);
}

static size_t observe_format(char *dst, size_t capacity, float reference, float feedback)
{
    CHECK(interrupt_mask == 0U);
    ++format_calls;
    interrupt_at(INSIDE_FORMAT);
    return TelemetryFormat_SpeedCsv(dst, capacity, reference, feedback);
}

static uint8_t *uart_buffer;
static uint16_t uart_length;
static int HAL_UART_Transmit_IT(UART_HandleTypeDef *port, uint8_t *data, uint16_t length)
{
    CHECK(interrupt_mask == 0U);
    CHECK(port == &huart1 && port->gState == HAL_UART_STATE_READY);
    ++uart_calls;
    interrupt_at(INSIDE_UART);
    if (uart_result == HAL_OK)
    {
        uart_buffer = data;
        uart_length = length;
        port->gState = HAL_UART_STATE_BUSY_TX;
    }
    return (int)uart_result;
}

#define TelemetryFormat_SpeedCsv observe_format
#include "telemetry_under_test.h"
#undef TelemetryFormat_SpeedCsv

static void run_background_due(uint32_t now)
{
#if AXIS_COMMAND_BENCH_ENABLE
    static struct { uint32_t telemetry_cycles; } work_timing;
    static uint32_t app_task_phase;
    uint32_t section_started;
#endif
    mock_tick = now;
#include "telemetry_cadence.h"
#if AXIS_COMMAND_BENCH_ENABLE
    (void)work_timing.telemetry_cycles;
    CHECK(app_task_phase == 0U);
#endif
}

static void sample(float reference, float feedback, uint32_t now)
{
    telemetry_ref_rpm = reference;
    telemetry_feedback_rpm = feedback;
    run_background_due(now);
}

static void check_uart_bytes(const char *expected)
{
    CHECK(uart_buffer == telemetry_buffer);
    CHECK(uart_length == strlen(expected));
    CHECK(memcmp(uart_buffer, expected, uart_length) == 0);
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


static void test_telemetry(void)
{
    unsigned before_checks = checks;
    CHECK(APP_TELEMETRY_PERIOD_MS == 5U);
    CHECK(current_telemetry_tx_count == 0U && current_telemetry_drop_count == 0U);
    huart1.gState = HAL_UART_STATE_READY;
    sample(1.0f, 2.0f, 4U); /* one tick too early */
    CHECK(format_calls == 0U && uart_calls == 0U);
    sample(12.5f, -3.0f, 5U);
#if AXIS_COMMAND_BENCH_ENABLE
    /* Empty/uncreated task or disabled stack measurement must not cause a
     * transmit from appTask. The first idle after creation consumes the pair. */
    CHECK(format_calls == 0U && uart_calls == 0U);
    telemetry_ref_rpm = 99.0f;
    telemetry_feedback_rpm = -88.0f;
    appTaskHandle = NULL;
    vApplicationIdleHook();
    CHECK(format_calls == 0U && uart_calls == 0U);
    appTaskHandle = &app_task_token;
    app_stack_probe_enabled = 0U;
    mock_tick = 9U; /* captured pair still usable at age 4 */
    vApplicationIdleHook();
#else
    /* Normal target must launch directly from the due step. */
    CHECK(format_calls == 1U && uart_calls == 1U);
    vApplicationIdleHook();
#endif
    CHECK(current_telemetry_tx_count == 1U && current_telemetry_drop_count == 0U);
    CHECK(format_calls == 1U && uart_calls == 1U);
    check_uart_bytes("12.500,-3.000\r\n");
    for (unsigned i = 0; i < 5U; ++i) vApplicationIdleHook();
    CHECK(format_calls == 1U && uart_calls == 1U);
    sample(300.0f, 400.0f, 10U); /* TX IRQ still owns buffer */
    CHECK(current_telemetry_drop_count == 1U && uart_calls == 1U);
    check_uart_bytes("12.500,-3.000\r\n");
    huart1.gState = HAL_UART_STATE_READY;
    sample(10000000.0f, 100.0f, 15U); /* outside CSV encoding range */
    vApplicationIdleHook();
    CHECK(current_telemetry_drop_count == 2U && current_telemetry_tx_count == 1U);
    CHECK(format_calls == 2U && uart_calls == 1U);
    check_uart_bytes("12.500,-3.000\r\n");
    uart_result = HAL_ERROR;
    sample(1.0f, 2.0f, 20U);
    vApplicationIdleHook();
    CHECK(format_calls == 3U && uart_calls == 2U);
    CHECK(current_telemetry_drop_count == 3U && current_telemetry_tx_count == 1U);
    uart_result = HAL_OK;
#if AXIS_COMMAND_BENCH_ENABLE
    sample(5.0f, 6.0f, 25U);
    mock_tick = 30U; /* exactly expired */
    vApplicationIdleHook();
    CHECK(uart_calls == 2U && current_telemetry_drop_count == 4U);
    vApplicationIdleHook();
    CHECK(current_telemetry_drop_count == 4U);
    sample(7.0f, 8.0f, 35U);
    huart1.gState = HAL_UART_STATE_BUSY_TX; /* check again at consumption */
    vApplicationIdleHook();
    CHECK(uart_calls == 2U && current_telemetry_drop_count == 5U);
    huart1.gState = HAL_UART_STATE_READY;
    sample(9.0f, -9.0f, 40U);
    interrupt_site = INSIDE_FORMAT;
    interrupt_tick = 45U;
    queue_on_interrupt = 0U;
    vApplicationIdleHook(); /* age advances without a second producer release */
    CHECK(interrupt_site == 0U && uart_calls == 2U && current_telemetry_drop_count == 6U);
    sample(50.5f, 51.0f, 50U);
    telemetry_ref_rpm = 55.0f;
    telemetry_feedback_rpm = 56.0f;
    interrupt_site = INSIDE_FORMAT;
    interrupt_tick = 55U;
    queue_on_interrupt = 1U;
    vApplicationIdleHook();
    CHECK(interrupt_site == 0U && uart_calls == 2U);
    CHECK(current_telemetry_drop_count == 8U); /* one due pair and one stale pair */
    sample(60.0f, -60.0f, 60U);
    telemetry_ref_rpm = 65.0f;
    telemetry_feedback_rpm = 66.0f;
    interrupt_site = INSIDE_UART;
    interrupt_tick = 65U;
    vApplicationIdleHook();
    CHECK(interrupt_site == 0U && uart_calls == 3U);
    CHECK(current_telemetry_tx_count == 2U && current_telemetry_drop_count == 9U);
    check_uart_bytes("60.000,-60.000\r\n");
    huart1.gState = HAL_UART_STATE_READY;
    sample(70.0f, 71.0f, 70U);
    run_background_due(75U); /* following due pair clashes with occupied slot */
    mock_tick = 71U; /* very late Idle with raw tick aliased near first due pair */
    vApplicationIdleHook();
    CHECK(uart_calls == 3U && current_telemetry_drop_count == 11U);
    sample(-72.0f, 73.0f, UINT32_MAX - 1U);
    mock_tick = 2U; /* four ticks later over wrap */
    vApplicationIdleHook();
    CHECK(uart_calls == 4U && current_telemetry_tx_count == 3U);
    check_uart_bytes("-72.000,73.000\r\n");
    huart1.gState = HAL_UART_STATE_READY;
    sample(74.0f, -75.0f, 3U);
    mock_tick = 8U;
    vApplicationIdleHook();
    CHECK(uart_calls == 4U && current_telemetry_drop_count == 12U);
    app_stack_probe_enabled = 1U;
#endif
    CHECK(interrupt_mask == 0U);
    printf("PASS: %s scheduled telemetry, %u checks, %u UART calls, %u successful, %u dropped\n",
           AXIS_COMMAND_BENCH_ENABLE ? "bench deferred" : "normal direct",
           checks - before_checks, uart_calls, (unsigned)current_telemetry_tx_count, (unsigned)current_telemetry_drop_count);
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
    test_telemetry();
    return 0;
}
