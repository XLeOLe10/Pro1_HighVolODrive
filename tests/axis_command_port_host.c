/* Tests the REAL axis_command.c + axis_command_core.c under a small CPU/RTOS
 * adapter. The interrupt scheduler is a deterministic model; it cannot prove
 * Cortex-M7 masking latency, electrical outputs, or adversarial memory writes. */
#include "main.h"
#include "board_safety.h"
#include "cmsis_os.h"
#include "board_config.h"
#include "axis_command.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned assertions;
#define CHECK(x) do { assertions++; if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

HostDwt host_dwt;
volatile uint32_t board_fault_mask;
volatile uint8_t board_output_enabled;
static int app_token, wrong_token;
osThreadId_t appTaskHandle = &app_token;
static osThreadId_t current_thread;
static osKernelState_t kernel_state;
static uint32_t host_mask, host_ipsr, host_now;
static unsigned disables, barriers, restores, irq_calls, safety_calls;
static uint32_t irq_pending, inject_on_disable;
static uint32_t inspect_disarm, disarm_active, disarm_epoch, disarm_seq;

static void DispatchIrq(void)
{
    uint32_t saved_ipsr = host_ipsr;
    CHECK(host_mask == 0U);
    host_ipsr = 25U;
    irq_calls++;
    (void)AxisCommandPort_BeginCycle(host_now, 1U);
    (void)AxisCommandPort_FinalPermit(host_now);
    host_ipsr = saved_ipsr;
}

static void RequestIrq(void)
{
    if (host_mask != 0U) irq_pending = 1U;
    else DispatchIrq();
}

uint32_t HAL_GetTick(void) { return host_now; }
uint32_t __get_PRIMASK(void) { return host_mask; }
uint32_t __get_IPSR(void) { return host_ipsr; }
void __disable_irq(void)
{
    host_mask = 1U;
    disables++;
    host_dwt.CYCCNT += 3U;
    if (inject_on_disable != 0U)
    {
        unsigned before = irq_calls;
        inject_on_disable = 0U;
        RequestIrq();
        CHECK(irq_pending != 0U);
        CHECK(irq_calls == before); /* Never consumed inside the locked copy. */
    }
}
void __DMB(void) { barriers++; host_dwt.CYCCNT += 5U; }
void __set_PRIMASK(uint32_t value)
{
    restores++;
    host_mask = value;
    if ((host_mask == 0U) && (irq_pending != 0U))
    {
        irq_pending = 0U;
        DispatchIrq();
    }
}
osKernelState_t osKernelGetState(void) { return kernel_state; }
osThreadId_t osThreadGetId(void) { return current_thread; }

void BoardSafety_DisableOutputs(void)
{
    safety_calls++;
    if (inspect_disarm != 0U)
    {
        AxisCommandSnapshot before;
        uint32_t saved_ipsr = host_ipsr;
        inspect_disarm = 0U;
        /* Instrumentation only: inspect core through its public coherent
         * reader BEFORE the hardware stub changes its output flag. This is
         * not a production task switch from inside TIM1. */
        host_ipsr = 0U;
        CHECK(AxisCommandPort_GetSnapshot(&before) == AXIS_CMD_OK);
        CHECK(before.active_valid == disarm_active);
        CHECK(before.run_epoch == disarm_epoch);
        CHECK(before.active.command_seq == disarm_seq);
        host_ipsr = saved_ipsr;
    }
    board_output_enabled = 0U;
}

static void Reset(void)
{
    host_mask = host_ipsr = host_now = irq_pending = inject_on_disable = 0U;
    disables = barriers = restores = irq_calls = safety_calls = 0U;
    inspect_disarm = 0U;
    board_fault_mask = 0U;
    board_output_enabled = 0U;
    host_dwt.CYCCNT = 0U;
    kernel_state = osKernelRunning;
    current_thread = appTaskHandle;
    AxisCommandPort_Init();
}

static AxisCommandFrame Frame(uint32_t epoch, uint32_t opcode,
                              float speed, float iq, uint32_t life)
{
    AxisCommandFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.run_epoch = epoch;
    frame.opcode = opcode;
    frame.control_mode = AXIS_CMD_SPEED_MODE;
    frame.speed_target_rpm = speed;
    frame.iq_limit_a = iq;
    frame.received_tick_ms = host_now;
    frame.expires_tick_ms = host_now + life;
    return frame;
}

static AxisCommandSnapshot Snapshot(void)
{
    AxisCommandSnapshot out;
    CHECK(AxisCommandPort_GetSnapshot(&out) == AXIS_CMD_OK);
    return out;
}

static void TestContextsAndMask(void)
{
    AxisCommandFrame frame;
    AxisCommandSnapshot out;
    uint32_t seq = 99U, ticket = 99U;
    unsigned before;
    Reset();
    frame = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
    CHECK(AxisCommandPort_GetSnapshot(NULL) == AXIS_CMD_INVALID);
    CHECK(AxisCommandPort_Submit(NULL, &seq) == AXIS_CMD_INVALID);
    CHECK(seq == 0U);
    CHECK(AxisCommandPort_Submit(&frame, NULL) == AXIS_CMD_INVALID);
    CHECK(AxisCommandPort_Stop(NULL) == AXIS_CMD_INVALID);
    host_ipsr = 25U;
    CHECK(AxisCommandPort_Submit(&frame, &seq) == AXIS_CMD_INVALID);
    CHECK(AxisCommandPort_Stop(&ticket) == AXIS_CMD_INVALID);
    CHECK(ticket == 0U);
    CHECK(AxisCommandPort_GetSnapshot(&out) == AXIS_CMD_INVALID);
    host_ipsr = 0U;
    kernel_state = osKernelInactive;
    CHECK(AxisCommandPort_Submit(&frame, &seq) == AXIS_CMD_INVALID);
    CHECK(AxisCommandPort_GetSnapshot(&out) == AXIS_CMD_INVALID);
    kernel_state = osKernelRunning;
    current_thread = &wrong_token;
    CHECK(AxisCommandPort_Submit(&frame, &seq) == AXIS_CMD_INVALID);
    CHECK(AxisCommandPort_Stop(&ticket) == AXIS_CMD_INVALID);
    CHECK(AxisCommandPort_GetSnapshot(&out) == AXIS_CMD_INVALID);
    current_thread = appTaskHandle;
    before = disables;
    CHECK(AxisCommandPort_GetEpoch() == 1U);
    CHECK(host_mask == 0U);
    CHECK(disables == before + 1U && barriers == disables && restores == disables);
    host_mask = 1U;
    out = Snapshot();
    CHECK(out.run_epoch == 1U && out.active_valid == 0U);
    CHECK(host_mask == 1U);
    CHECK(barriers == disables && restores == disables);
    CHECK(axis_command_critical_max_cycles > 0U);
    host_mask = 0U;
}

#if AXIS_COMMAND_BENCH_ENABLE
static void TestRealPublicationAndSnapshot(void)
{
    AxisCommandFrame a, b, expected;
    AxisCommandSnapshot out;
    uint32_t seq_a, seq_b, ticket, same_ticket;
    unsigned before;
    Reset();
    CHECK(axis_command_configured == 1U);
    a = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
    inject_on_disable = 1U;
    CHECK(AxisCommandPort_Submit(&a, &seq_a) == AXIS_CMD_OK);
    CHECK(host_mask == 0U && irq_pending == 0U && irq_calls == 1U);
    CHECK(a.command_seq == 0U); /* Caller's frame is not changed. */
    out = Snapshot();
    expected = a; expected.command_seq = seq_a;
    CHECK(memcmp(&out.active, &expected, sizeof(expected)) == 0);
    CHECK(out.applied_seq == seq_a && out.result_seq == seq_a);
    CHECK(out.result == AXIS_CMD_APPLIED && out.permit == 1U);
    CHECK(out.applied_control_seq == 1U && out.control_seq == 1U);

    b = Frame(1U, AXIS_CMD_UPDATE, -200.0f, 0.5f, 500U);
    host_mask = 1U;
    CHECK(AxisCommandPort_Submit(&b, &seq_b) == AXIS_CMD_OK);
    CHECK(host_mask == 1U && irq_calls == 1U);
    /* Make TIM1 pending at snapshot entry. The returned snapshot is all A,
     * even though unmasking at the END applies all B before the API returns. */
    host_mask = 0U;
    inject_on_disable = 1U;
    out = Snapshot();
    CHECK(out.active.speed_target_rpm == 100.0f && out.active.iq_limit_a == 1.0f);
    CHECK(out.active.command_seq == seq_a && out.applied_seq == seq_a);
    CHECK(out.control_seq == 1U && irq_calls == 2U);
    out = Snapshot();
    expected = b; expected.command_seq = seq_b;
    CHECK(memcmp(&out.active, &expected, sizeof(expected)) == 0);
    CHECK(out.applied_seq == seq_b && out.applied_control_seq == 2U);
    CHECK(out.control_seq == 2U && out.run_epoch == 1U);

    before = safety_calls;
    CHECK(AxisCommandPort_Stop(&ticket) == AXIS_CMD_OK);
    CHECK(AxisCommandPort_Stop(&same_ticket) == AXIS_CMD_OK);
    CHECK(same_ticket == ticket);
    inspect_disarm = 1U; disarm_active = 1U; disarm_epoch = 1U; disarm_seq = seq_b;
    RequestIrq();
    CHECK(safety_calls > before && inspect_disarm == 0U);
    out = Snapshot();
    CHECK(out.active_valid == 0U && out.permit == 0U && out.run_epoch == 2U);
    CHECK(out.stop_applied_ticket == ticket && out.result == AXIS_CMD_STOPPED);
    CHECK(AxisCommandPort_Submit(&a, &seq_a) == AXIS_CMD_STALE_EPOCH);
    CHECK(seq_a == 0U);
    CHECK(disables == barriers && barriers == restores);
}

static void TestFaultsAndExpiry(void)
{
    AxisCommandFrame f;
    AxisCommandSnapshot out;
    uint32_t seq, active_seq;
    unsigned before;
    Reset();
    f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
    CHECK(AxisCommandPort_Submit(&f, &seq) == AXIS_CMD_OK);
    board_fault_mask = 1U; /* Fault AFTER publication, BEFORE consumption. */
    RequestIrq();
    out = Snapshot();
    CHECK(out.active_valid == 0U && out.applied_seq == 0U && out.permit == 0U);
    CHECK(out.cancelled_seq == seq && out.result == AXIS_CMD_FAULT);
    board_fault_mask = 0U;
    f.run_epoch = out.run_epoch;
    CHECK(AxisCommandPort_Submit(&f, &seq) == AXIS_CMD_FAULT); /* Sticky. */

    Reset();
    f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
    CHECK(AxisCommandPort_Submit(&f, &seq) == AXIS_CMD_OK);
    RequestIrq();
    active_seq = seq;
    board_output_enabled = 1U; /* MODEL ONLY: test disarm ordering. */
    board_fault_mask = 8U;
    inspect_disarm = 1U; disarm_active = 1U; disarm_epoch = 1U; disarm_seq = seq;
    before = safety_calls;
    host_ipsr = 25U;
    CHECK(AxisCommandPort_FinalPermit(host_now) == 0U);
    host_ipsr = 0U;
    CHECK(safety_calls == before + 1U && board_output_enabled == 0U);
    out = Snapshot();
    CHECK(out.active_valid == 0U && out.run_epoch == 2U);
    CHECK(out.result == AXIS_CMD_FAULT && out.result_seq == active_seq);

    Reset();
    host_now = 0xfffffff0U;
    f = Frame(1U, AXIS_CMD_START, -100.0f, 0.5f, 64U);
    CHECK(AxisCommandPort_Submit(&f, &seq) == AXIS_CMD_OK);
    RequestIrq();
    host_now = 0x10U;
    CHECK(AxisCommandPort_FinalPermit(host_now) == 1U);
    host_now = 0x30U;
    inspect_disarm = 1U; disarm_active = 1U; disarm_epoch = 1U; disarm_seq = seq;
    CHECK(AxisCommandPort_FinalPermit(host_now) == 0U);
    out = Snapshot();
    CHECK(out.active_valid == 0U && out.run_epoch == 2U && out.result == AXIS_CMD_EXPIRED);
    CHECK(inspect_disarm == 0U);
    host_now += 100U; RequestIrq();
    out = Snapshot();
    CHECK(out.active_valid == 0U && out.permit == 0U && out.run_epoch == 2U);
}
#else
static void TestNormalProfile(void)
{
    AxisCommandFrame f;
    AxisCommandSnapshot out;
    uint32_t seq = 99U, ticket;
    Reset();
    CHECK(axis_command_configured == 0U);
    f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
    CHECK(AxisCommandPort_Submit(&f, &seq) == AXIS_CMD_CONFIG_UNSET);
    CHECK(seq == 0U && disables == 0U); /* Validation failed before locking. */
    CHECK(AxisCommandPort_Stop(&ticket) == AXIS_CMD_OK);
    RequestIrq();
    out = Snapshot();
    CHECK(out.active_valid == 0U && out.permit == 0U);
    CHECK(out.stop_applied_ticket == ticket && out.run_epoch == 2U);
    CHECK(out.result == AXIS_CMD_STOPPED);
}
#endif


static float FloatBits(uint32_t bits)
{
    float value;
    memcpy(&value,&bits,sizeof(value));
    return value;
}
static void TestTargetPrecheck(void)
{
    static const float speeds[]={0.0f,1000.0f,-1000.0f,1001.0f,-1001.0f,0.0f,100.0f};
    static const float currents[]={0.0f,4.0f,4.0f,0.5f,0.5f,-0.001f,4.001f};
    AxisCommandSnapshot before,after;
    unsigned i,locked,barrier_before,safety_before;
    Reset();
    host_ipsr=25U;
    CHECK(AxisCommandPort_CheckTargets(100.0f,0.5f)==AXIS_CMD_INVALID_CONTEXT);
    host_ipsr=0U;kernel_state=osKernelInactive;
    CHECK(AxisCommandPort_CheckTargets(100.0f,0.5f)==AXIS_CMD_INVALID_CONTEXT);
    kernel_state=osKernelRunning;current_thread=&wrong_token;
    CHECK(AxisCommandPort_CheckTargets(100.0f,0.5f)==AXIS_CMD_INVALID_CONTEXT);
    current_thread=appTaskHandle;
    before=Snapshot();locked=disables;barrier_before=barriers;safety_before=safety_calls;
    for(i=0U;i<sizeof(speeds)/sizeof(speeds[0]);++i) {
#if AXIS_COMMAND_BENCH_ENABLE
        uint32_t expected=i<3U?AXIS_CMD_OK:AXIS_CMD_INVALID;
#else
        uint32_t expected=AXIS_CMD_CONFIG_UNSET;
#endif
        CHECK(AxisCommandPort_CheckTargets(speeds[i],currents[i])==expected);
    }
#if AXIS_COMMAND_BENCH_ENABLE
    CHECK(AxisCommandPort_CheckTargets(FloatBits(0x7FC00000U),0.5f)==AXIS_CMD_INVALID);
    CHECK(AxisCommandPort_CheckTargets(100.0f,FloatBits(0x7FC00000U))==AXIS_CMD_INVALID);
    CHECK(AxisCommandPort_CheckTargets(FloatBits(0x7F800000U),0.5f)==AXIS_CMD_INVALID);
    CHECK(AxisCommandPort_CheckTargets(FloatBits(0xFF800000U),0.5f)==AXIS_CMD_INVALID);
    CHECK(AxisCommandPort_CheckTargets(100.0f,FloatBits(0x7F800000U))==AXIS_CMD_INVALID);
    CHECK(AxisCommandPort_CheckTargets(-0.0f,-0.0f)==AXIS_CMD_OK);
#else
    CHECK(AxisCommandPort_CheckTargets(FloatBits(0x7FC00000U),0.5f)==AXIS_CMD_CONFIG_UNSET);
#endif
    CHECK(disables==locked && barriers==barrier_before && safety_calls==safety_before);
    CHECK(host_mask==0U && irq_calls==0U && board_fault_mask==0U && board_output_enabled==0U);
    after=Snapshot();
    CHECK(memcmp(&before,&after,sizeof(before))==0);
    CHECK(axis_command_result==before.result && axis_command_result_seq==before.result_seq);
}
#if AXIS_COMMAND_BENCH_ENABLE
static void TestPrecheckCannotPublishOrRenew(void)
{
    AxisCommandFrame f;
    AxisCommandSnapshot before,after;
    uint32_t seq_a,seq_b;
    unsigned locked,safety_before;
    Reset();
    f=Frame(1U,AXIS_CMD_START,100.0f,1.0f,500U);
    CHECK(AxisCommandPort_Submit(&f,&seq_a)==AXIS_CMD_OK);RequestIrq();
    f=Frame(1U,AXIS_CMD_UPDATE,-200.0f,2.0f,500U);
    CHECK(AxisCommandPort_Submit(&f,&seq_b)==AXIS_CMD_OK);
    host_now=499U;
    before=Snapshot();locked=disables;safety_before=safety_calls;
    CHECK(AxisCommandPort_CheckTargets(700.0f,3.0f)==AXIS_CMD_OK);
    CHECK(AxisCommandPort_CheckTargets(700.0f,3.0f)==AXIS_CMD_OK);
    CHECK(AxisCommandPort_CheckTargets(-1001.0f,4.001f)==AXIS_CMD_INVALID);
    CHECK(disables==locked && safety_calls==safety_before);
    after=Snapshot();CHECK(memcmp(&before,&after,sizeof(before))==0);
    RequestIrq();after=Snapshot();
    CHECK(after.applied_seq==seq_b && after.applied_seq!=seq_a);
    CHECK(after.active.speed_target_rpm==-200.0f && after.active.iq_limit_a==2.0f);
    CHECK(after.active.received_tick_ms==0U && after.active.expires_tick_ms==500U);
    host_now=500U;RequestIrq();after=Snapshot();
    CHECK(after.active_valid==0U && after.permit==0U && after.result==AXIS_CMD_EXPIRED);
    before=after;
    CHECK(AxisCommandPort_CheckTargets(100.0f,0.5f)==AXIS_CMD_OK);
    after=Snapshot();CHECK(memcmp(&before,&after,sizeof(before))==0);
    board_fault_mask=0x40U;RequestIrq();before=Snapshot();
    CHECK(AxisCommandPort_CheckTargets(100.0f,0.5f)==AXIS_CMD_OK);
    after=Snapshot();CHECK(memcmp(&before,&after,sizeof(before))==0);
    CHECK(board_fault_mask==0x40U && board_output_enabled==0U);
}
#endif

int main(void)
{
    TestContextsAndMask();
    TestTargetPrecheck();
#if AXIS_COMMAND_BENCH_ENABLE
    TestRealPublicationAndSnapshot();
    TestFaultsAndExpiry();
    TestPrecheckCannotPublishOrRenew();
#else
    TestNormalProfile();
#endif
    printf("PASS: %u real command port assertions (BENCH=%u); deterministic CPU stubs only\n",
           assertions, (unsigned)AXIS_COMMAND_BENCH_ENABLE);
    return 0;
}
