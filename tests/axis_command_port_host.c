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
#if AXIS_COMMAND_BENCH_ENABLE
static uint32_t clock_tick, clock_trusted;
static unsigned lease_calls, fail_lease_call;
/* Deterministic raw-clock adapter only; real FDCAN trust/config/TOO behavior is
 * verified by the CAN service HOST and ultimately by the board, not this stub. */
uint32_t CanLink_LeaseValid(uint32_t origin, uint32_t lifetime)
{
    ++lease_calls;
    if (lease_calls == fail_lease_call) return 0U;
    return clock_trusted && origin <= 0xFFFFU && lifetime != 0U &&
        lifetime <= 31250U && (uint32_t)(uint16_t)(clock_tick-origin) < lifetime;
}
#endif

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
#if AXIS_COMMAND_BENCH_ENABLE
    clock_tick = 0U; clock_trusted = 1U; lease_calls = fail_lease_call = 0U;
#endif
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

static AxisCommandReceipt Receipt(void)
{
    AxisCommandReceipt out;
    CHECK(AxisCommandPort_GetReceipt(&out) == AXIS_CMD_OK);
    return out;
}

static void TestReceiptContextsAndMask(void)
{
    AxisCommandReceipt out, before, empty;
    AxisCommandSnapshot state_before, state_after;
    unsigned locked, safety_before;
    Reset();
    CHECK(sizeof(AxisCommandReceipt) == 12U && sizeof(AxisCommandSnapshot) == 72U);
    memset(&out, 0xA5, sizeof(out)); before = out;
    CHECK(AxisCommandPort_GetReceipt(NULL) == AXIS_CMD_INVALID);
    host_ipsr = 25U;
    CHECK(AxisCommandPort_GetReceipt(&out) == AXIS_CMD_INVALID);
    CHECK(memcmp(&before, &out, sizeof(out)) == 0);
    host_ipsr = 0U; kernel_state = osKernelInactive;
    CHECK(AxisCommandPort_GetReceipt(&out) == AXIS_CMD_INVALID);
    CHECK(memcmp(&before, &out, sizeof(out)) == 0);
    kernel_state = osKernelRunning; current_thread = &wrong_token;
    CHECK(AxisCommandPort_GetReceipt(&out) == AXIS_CMD_INVALID);
    CHECK(memcmp(&before, &out, sizeof(out)) == 0);
    CHECK(disables == 0U && barriers == 0U && restores == 0U && host_mask == 0U);
    current_thread = appTaskHandle;
    empty = Receipt();
    CHECK(empty.command_seq == 0U && empty.result == 0U && empty.control_seq == 0U);
    CHECK(host_mask == 0U && disables == 1U && barriers == 1U && restores == 1U);
    host_mask = 1U; out = Receipt();
    CHECK(host_mask == 1U && memcmp(&empty, &out, sizeof(out)) == 0);
    CHECK(disables == 2U && barriers == 2U && restores == 2U && irq_calls == 0U);
    host_mask = 0U;
    state_before = Snapshot(); locked = disables; safety_before = safety_calls;
    out = Receipt(); before = Receipt();
    CHECK(memcmp(&out, &before, sizeof(out)) == 0);
    CHECK(disables == locked + 2U && barriers == disables && restores == disables);
    CHECK(safety_calls == safety_before && board_output_enabled == 0U && board_fault_mask == 0U);
    state_after = Snapshot();
    CHECK(memcmp(&state_before, &state_after, sizeof(state_before)) == 0);
}

#if AXIS_COMMAND_BENCH_ENABLE
static void CheckReceiptUnchanged(const AxisCommandReceipt *expected)
{
    AxisCommandReceipt actual = Receipt();
    CHECK(memcmp(expected, &actual, sizeof(actual)) == 0);
}

static void TestReceiptAppliedAndReplaced(void)
{
    AxisCommandFrame f;
    AxisCommandReceipt first, second;
    AxisCommandSnapshot before, after;
    uint32_t seq_a, seq_b, rejected_seq = 99U, ticket;
    unsigned safety_before;
    Reset();
    f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
    CHECK(AxisCommandPort_Submit(&f, &seq_a) == AXIS_CMD_OK);
    first = Receipt(); CHECK(first.command_seq == 0U); /* Accepted is not applied. */
    RequestIrq(); first = Receipt();
    CHECK(first.command_seq == seq_a && first.result == AXIS_CMD_APPLIED && first.control_seq == 1U);
    before = Snapshot(); safety_before = safety_calls;
    CheckReceiptUnchanged(&first); CheckReceiptUnchanged(&first);
    /* Existing debugger mirrors cannot replace the private completion record. */
    axis_command_result = AXIS_CMD_FAULT; axis_command_result_seq = 0xFFFFFFFFU;
    axis_command_applied_seq = 0U; axis_command_control_seq = 0U;
    CheckReceiptUnchanged(&first);
    after = Snapshot(); CHECK(memcmp(&before, &after, sizeof(before)) == 0);
    CHECK(safety_calls == safety_before && board_fault_mask == 0U);
    f = Frame(1U, AXIS_CMD_UPDATE, -200.0f, 0.5f, 500U);
    CHECK(AxisCommandPort_Submit(&f, &seq_b) == AXIS_CMD_OK);
    CheckReceiptUnchanged(&first); /* A new pending frame does not clear history. */
    CHECK(AxisCommandPort_Submit(&f, &rejected_seq) == AXIS_CMD_BUSY && rejected_seq == 0U);
    CheckReceiptUnchanged(&first);
    RequestIrq(); second = Receipt();
    CHECK(second.command_seq == seq_b && second.result == AXIS_CMD_APPLIED && second.control_seq == 2U);
    CHECK(AxisCommandPort_Stop(&ticket) == AXIS_CMD_OK); RequestIrq();
    CheckReceiptUnchanged(&second);
    CHECK(AxisCommandPort_Stop(&ticket) == AXIS_CMD_OK); RequestIrq();
    CheckReceiptUnchanged(&second); /* Empty STOP cannot erase the completed UPDATE. */
    CHECK(AxisCommandPort_Submit(&f, &rejected_seq) == AXIS_CMD_STALE_EPOCH && rejected_seq == 0U);
    CheckReceiptUnchanged(&second);
    after = Snapshot();
    CHECK(after.active_valid == 0U && after.permit == 0U && after.applied_seq == seq_b);
    CHECK(after.stop_applied_ticket == ticket && after.control_seq > second.control_seq);
}

static void TestReceiptActiveLoss(void)
{
    unsigned mode;
    for (mode = 0U; mode < 3U; ++mode)
    {
        AxisCommandFrame f;
        AxisCommandReceipt applied;
        AxisCommandSnapshot state;
        uint32_t seq;
        unsigned safety_before;
        Reset(); f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
        CHECK(AxisCommandPort_Submit(&f, &seq) == AXIS_CMD_OK); RequestIrq();
        applied = Receipt();
        board_output_enabled = 1U; /* CPU model only; no real output operation. */
        safety_before = safety_calls;
        if (mode == 0U) { host_now = 500U; RequestIrq(); }
        else if (mode == 1U)
        {
            board_fault_mask = 8U; host_ipsr = 25U;
            CHECK(AxisCommandPort_FinalPermit(host_now) == 0U); host_ipsr = 0U;
        }
        else
        {
            host_ipsr = 25U;
            CHECK(AxisCommandPort_BeginCycle(host_now, 0U) == NULL);
            CHECK(AxisCommandPort_FinalPermit(host_now) == 0U); host_ipsr = 0U;
        }
        CheckReceiptUnchanged(&applied);
        state = Snapshot();
        CHECK(state.active_valid == 0U && state.permit == 0U && state.applied_seq == seq);
        CHECK(state.result == (mode == 0U ? AXIS_CMD_EXPIRED :
                              (mode == 1U ? AXIS_CMD_FAULT : AXIS_CMD_NOT_READY)));
        CHECK(safety_calls > safety_before && board_output_enabled == 0U);
        RequestIrq(); CheckReceiptUnchanged(&applied);
    }
}

static void TestReceiptRejectedAndRetained(void)
{
    AxisCommandFrame f;
    AxisCommandReceipt rejected;
    AxisCommandSnapshot state;
    uint32_t seq, rejected_seq, ticket;
    Reset(); f = Frame(1U, AXIS_CMD_UPDATE, 100.0f, 1.0f, 500U);
    CHECK(AxisCommandPort_Submit(&f, &seq) == AXIS_CMD_OK); RequestIrq();
    rejected = Receipt();
    CHECK(rejected.command_seq == seq && rejected.result == AXIS_CMD_NOT_RUNNING && rejected.control_seq == 1U);
    CHECK(AxisCommandPort_Stop(&ticket) == AXIS_CMD_OK); RequestIrq();
    CheckReceiptUnchanged(&rejected);
    f = Frame(AxisCommandPort_GetEpoch(), AXIS_CMD_START, 100.0f, 1.0f, 500U);
    CHECK(AxisCommandPort_Submit(&f, &seq) == AXIS_CMD_OK);
    host_ipsr = 25U;
    CHECK(AxisCommandPort_BeginCycle(host_now, 0U) == NULL);
    CHECK(AxisCommandPort_FinalPermit(host_now) == 0U); host_ipsr = 0U;
    rejected = Receipt();
    CHECK(rejected.command_seq == seq && rejected.result == AXIS_CMD_NOT_READY && rejected.control_seq == 3U);
    f.run_epoch = 1U;
    CHECK(AxisCommandPort_Submit(&f, &rejected_seq) == AXIS_CMD_STALE_EPOCH && rejected_seq == 0U);
    CheckReceiptUnchanged(&rejected);
    CHECK(AxisCommandPort_Stop(&ticket) == AXIS_CMD_OK); RequestIrq();
    CheckReceiptUnchanged(&rejected);
    state = Snapshot(); CHECK(state.result == AXIS_CMD_STOPPED && state.applied_seq == 0U);

    Reset(); f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
    CHECK(AxisCommandPort_Submit(&f, &seq) == AXIS_CMD_OK); RequestIrq();
    CHECK(AxisCommandPort_Submit(&f, &seq) == AXIS_CMD_OK); RequestIrq();
    rejected = Receipt();
    CHECK(rejected.command_seq == seq && rejected.result == AXIS_CMD_ALREADY_RUNNING && rejected.control_seq == 2U);
    CHECK(AxisCommandPort_Stop(&ticket) == AXIS_CMD_OK); RequestIrq();
    CheckReceiptUnchanged(&rejected); /* Revoking old active A must not overwrite rejected B. */
    state = Snapshot(); CHECK(state.active_valid == 0U && state.applied_seq != seq);
}

static void TestReceiptPendingCancellation(void)
{
    unsigned mode;
    for (mode = 0U; mode < 6U; ++mode)
    {
        AxisCommandFrame f;
        AxisCommandReceipt receipt;
        AxisCommandSnapshot state;
        uint32_t active_seq = 0U, pending_seq, ticket = 0U;
        uint32_t reason = (mode < 2U) ? AXIS_CMD_STOPPED :
                          ((mode == 2U || mode == 5U) ? AXIS_CMD_FAULT :
                           (mode == 3U ? AXIS_CMD_EXPIRED : AXIS_CMD_NOT_READY));
        Reset();
        if (mode != 0U && mode != 5U)
        {
            f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
            CHECK(AxisCommandPort_Submit(&f, &active_seq) == AXIS_CMD_OK); RequestIrq();
        }
        if (mode == 3U) host_now = 499U; /* Pending B is fresh when active A expires. */
        f = Frame(1U, active_seq ? AXIS_CMD_UPDATE : AXIS_CMD_START, -100.0f, 0.5f, 500U);
        CHECK(AxisCommandPort_Submit(&f, &pending_seq) == AXIS_CMD_OK);
        receipt = Receipt(); CHECK(receipt.command_seq == active_seq);
        if (mode < 3U) CHECK(AxisCommandPort_Stop(&ticket) == AXIS_CMD_OK);
        if (mode == 2U || mode == 5U) board_fault_mask = 8U;
        if (mode == 3U) host_now = 500U;
        if (mode == 4U)
        {
            host_ipsr = 25U;
            CHECK(AxisCommandPort_BeginCycle(host_now, 0U) == NULL);
            CHECK(AxisCommandPort_FinalPermit(host_now) == 0U); host_ipsr = 0U;
        }
        else RequestIrq();
        receipt = Receipt(); state = Snapshot();
        CHECK(receipt.command_seq == pending_seq && receipt.result == reason);
        CHECK(receipt.control_seq == state.control_seq);
        CHECK(state.applied_seq == active_seq && state.cancelled_seq == pending_seq);
        CHECK(state.active_valid == 0U && state.permit == 0U && board_output_enabled == 0U);
        if (mode < 3U) CHECK(state.stop_applied_ticket == ticket);
        CHECK(AxisCommandPort_Stop(&ticket) == AXIS_CMD_OK); RequestIrq();
        CheckReceiptUnchanged(&receipt);
    }
}

static void TestReceiptDeferredInterrupt(void)
{
    AxisCommandFrame f;
    AxisCommandReceipt first, observed, second, rejected;
    uint32_t seq_a, seq_b, seq_c;
    Reset(); f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
    CHECK(AxisCommandPort_Submit(&f, &seq_a) == AXIS_CMD_OK); RequestIrq(); first = Receipt();
    f = Frame(1U, AXIS_CMD_UPDATE, -100.0f, 0.5f, 500U);
    CHECK(AxisCommandPort_Submit(&f, &seq_b) == AXIS_CMD_OK);
    inject_on_disable = 1U; observed = Receipt();
    CHECK(memcmp(&first, &observed, sizeof(first)) == 0);
    CHECK(host_mask == 0U && irq_pending == 0U && irq_calls == 2U);
    second = Receipt();
    CHECK(second.command_seq == seq_b && second.result == AXIS_CMD_APPLIED && second.control_seq == 2U);
    f = Frame(1U, AXIS_CMD_START, 200.0f, 2.0f, 500U);
    CHECK(AxisCommandPort_Submit(&f, &seq_c) == AXIS_CMD_OK);
    inject_on_disable = 1U; observed = Receipt();
    CHECK(memcmp(&second, &observed, sizeof(second)) == 0);
    CHECK(host_mask == 0U && irq_pending == 0U && irq_calls == 3U);
    rejected = Receipt();
    CHECK(rejected.command_seq == seq_c && rejected.result == AXIS_CMD_ALREADY_RUNNING && rejected.control_seq == 3U);
    CHECK(disables == barriers && barriers == restores);
}
#else
static void TestReceiptNormalProfile(void)
{
    AxisCommandFrame f;
    AxisCommandReceipt empty, after;
    uint32_t seq, ticket;
    Reset(); empty = Receipt();
    f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
    CHECK(AxisCommandPort_Submit(&f, &seq) == AXIS_CMD_CONFIG_UNSET && seq == 0U);
    CHECK(AxisCommandPort_Stop(&ticket) == AXIS_CMD_OK); RequestIrq();
    after = Receipt();
    CHECK(memcmp(&empty, &after, sizeof(empty)) == 0 && after.command_seq == 0U);
}
#endif

static void TestTimedContextAndProfile(void)
{
    AxisCommandFrame f;
    uint32_t seq = 99U;
    Reset(); f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
    CHECK(AxisCommandPort_SubmitTimed(NULL, 0U, 1U, &seq) == AXIS_CMD_INVALID && seq == 0U);
    CHECK(AxisCommandPort_SubmitTimed(&f, 0U, 1U, NULL) == AXIS_CMD_INVALID);
    host_ipsr = 25U;
    CHECK(AxisCommandPort_SubmitTimed(&f, 0U, 1U, &seq) == AXIS_CMD_INVALID);
    host_ipsr = 0U; current_thread = &wrong_token;
    CHECK(AxisCommandPort_SubmitTimed(&f, 0U, 1U, &seq) == AXIS_CMD_INVALID);
    current_thread = appTaskHandle; kernel_state = osKernelInactive;
    CHECK(AxisCommandPort_SubmitTimed(&f, 0U, 1U, &seq) == AXIS_CMD_INVALID);
    kernel_state = osKernelRunning;
#if AXIS_COMMAND_BENCH_ENABLE
    CHECK(AxisCommandPort_SubmitTimed(&f, 65536U, 1U, &seq) == AXIS_CMD_INVALID);
    CHECK(AxisCommandPort_SubmitTimed(&f, 0U, 0U, &seq) == AXIS_CMD_INVALID);
    CHECK(AxisCommandPort_SubmitTimed(&f, 0U, 31251U, &seq) == AXIS_CMD_INVALID);
    CHECK(seq == 0U && lease_calls == 0U && disables == 0U);
    CHECK(AxisCommandPort_SubmitTimed(&f, 0U, 31250U, &seq) == AXIS_CMD_OK);
    RequestIrq(); CHECK(Snapshot().active.expires_tick_ms == 1000U);
#else
    CHECK(AxisCommandPort_SubmitTimed(&f, 0U, 100U, &seq) == AXIS_CMD_UNSUPPORTED_MODE);
    CHECK(seq == 0U && disables == 0U);
#endif
}

#if AXIS_COMMAND_BENCH_ENABLE
static void TestTimedAtomicAndRawDeadline(void)
{
    AxisCommandFrame f, original;
    AxisCommandSnapshot out;
    AxisCommandReceipt applied;
    uint32_t seq;
    Reset(); host_now = 5U; clock_tick = 0U;
    f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
    f.received_tick_ms = 0x80000000U; f.expires_tick_ms = 0U; original = f;
    inject_on_disable = 1U;
    CHECK(AxisCommandPort_SubmitTimed(&f, 0xFFF0U, 100U, &seq) == AXIS_CMD_OK);
    CHECK(irq_calls == 1U && host_mask == 0U && irq_pending == 0U);
    CHECK(memcmp(&f, &original, sizeof(f)) == 0);
    out = Snapshot(); applied = Receipt();
    CHECK(out.active_valid == 1U && out.permit == 1U && out.active.command_seq == seq);
    CHECK(out.active.received_tick_ms == 5U && out.active.expires_tick_ms == 9U);
    CHECK(applied.command_seq == seq && applied.result == AXIS_CMD_APPLIED);
    clock_tick = 0x53U; RequestIrq(); CHECK(Snapshot().active_valid == 1U);
    /* HAL time is frozen; original raw SOF deadline still wins at exactly 100 ticks. */
    clock_tick = 0x54U; board_output_enabled = 1U;
    inspect_disarm = 1U; disarm_active = 1U; disarm_epoch = 1U; disarm_seq = seq;
    RequestIrq(); out = Snapshot();
    CHECK(out.active_valid == 0U && out.permit == 0U && out.result == AXIS_CMD_EXPIRED);
    CHECK(out.run_epoch == 2U && board_output_enabled == 0U && inspect_disarm == 0U);
    CheckReceiptUnchanged(&applied);
}

static void TestTimedPublishRecheck(void)
{
    AxisCommandFrame f;
    AxisCommandSnapshot out;
    uint32_t seq = 99U;
    Reset(); f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
    fail_lease_call = 2U; inject_on_disable = 1U;
    CHECK(AxisCommandPort_SubmitTimed(&f, 0U, 100U, &seq) == AXIS_CMD_EXPIRED);
    CHECK(seq == 0U && irq_calls == 1U && host_mask == 0U && lease_calls == 2U);
    out = Snapshot(); CHECK(out.active_valid == 0U && out.applied_seq == 0U);
    CHECK(Receipt().command_seq == 0U);
    clock_trusted = 0U; seq = 99U;
    CHECK(AxisCommandPort_SubmitTimed(&f, 0U, 100U, &seq) == AXIS_CMD_EXPIRED && seq == 0U);
    clock_trusted = 1U; clock_tick = 100U;
    CHECK(AxisCommandPort_SubmitTimed(&f, 0U, 100U, &seq) == AXIS_CMD_EXPIRED && seq == 0U);
}

static void TestTimedActiveAndPendingExpiry(void)
{
    unsigned mode;
    for (mode = 0U; mode < 2U; ++mode)
    {
        AxisCommandFrame f;
        AxisCommandSnapshot out;
        AxisCommandReceipt receipt;
        uint32_t active_seq, pending_seq, old_control;
        Reset(); clock_tick = 100U;
        f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
        CHECK(AxisCommandPort_SubmitTimed(&f, 100U, mode ? 1000U : 100U, &active_seq) == AXIS_CMD_OK);
        RequestIrq(); old_control = Snapshot().control_seq;
        clock_tick = mode ? 100U : 199U;
        f = Frame(1U, AXIS_CMD_UPDATE, -100.0f, 2.0f, 500U);
        CHECK(AxisCommandPort_SubmitTimed(&f, clock_tick, mode ? 1U : 100U, &pending_seq) == AXIS_CMD_OK);
        clock_tick = mode ? 101U : 200U; board_output_enabled = 1U;
        inspect_disarm = 1U; disarm_active = 1U; disarm_epoch = 1U; disarm_seq = active_seq;
        RequestIrq(); out = Snapshot(); receipt = Receipt();
        CHECK(out.active_valid == 0U && out.run_epoch == 2U && out.applied_seq == active_seq);
        CHECK(out.cancelled_seq == pending_seq && out.result == AXIS_CMD_EXPIRED);
        CHECK(receipt.command_seq == pending_seq && receipt.result == AXIS_CMD_EXPIRED);
        CHECK(receipt.control_seq == old_control && out.control_seq == old_control + 1U);
        CHECK(board_output_enabled == 0U && inspect_disarm == 0U);
    }
}

static void TestTimedFinalGateAndPause(void)
{
    unsigned mode;
    for (mode = 0U; mode < 4U; ++mode)
    {
        AxisCommandFrame f;
        AxisCommandSnapshot out;
        AxisCommandReceipt receipt;
        uint32_t seq;
        Reset(); clock_tick = 500U;
        f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
        CHECK(AxisCommandPort_SubmitTimed(&f, 500U, 10U, &seq) == AXIS_CMD_OK);
        if (mode != 3U) RequestIrq(); /* mode 3 loses time while only pending. */
        if (mode == 0U) clock_tick = 510U;
        else clock_trusted = 0U; /* TOO/clock trust fails, HAL remains unchanged. */
        if (mode < 2U)
        {
            board_output_enabled = 1U;
            inspect_disarm = 1U; disarm_active = 1U; disarm_epoch = 1U; disarm_seq = seq;
            host_ipsr = 25U;
            CHECK(AxisCommandPort_FinalPermit(host_now) == 0U);
            host_ipsr = 0U;
            CHECK(inspect_disarm == 0U && board_output_enabled == 0U);
        }
        else RequestIrq();
        out = Snapshot(); receipt = Receipt();
        CHECK(out.active_valid == 0U && out.permit == 0U && out.result == AXIS_CMD_EXPIRED);
        CHECK(out.run_epoch == 2U && receipt.command_seq == seq);
        CHECK(receipt.result == (mode == 3U ? AXIS_CMD_EXPIRED : AXIS_CMD_APPLIED));
        clock_trusted = 1U; clock_tick = 500U; RequestIrq();
        CHECK(Snapshot().active_valid == 0U); /* Wrapped/restored clocks cannot revive it. */
    }
}

static void TestTimedStopAndFaultPrecedence(void)
{
    unsigned mode;
    for (mode = 0U; mode < 2U; ++mode)
    {
        AxisCommandFrame f;
        AxisCommandReceipt receipt;
        AxisCommandSnapshot out;
        uint32_t seq, ticket;
        Reset(); f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
        CHECK(AxisCommandPort_SubmitTimed(&f, 0U, 100U, &seq) == AXIS_CMD_OK);
        CHECK(AxisCommandPort_Stop(&ticket) == AXIS_CMD_OK);
        clock_trusted = 0U; if (mode) board_fault_mask = 8U;
        RequestIrq(); out = Snapshot(); receipt = Receipt();
        CHECK(receipt.command_seq == seq && receipt.result == (mode ? AXIS_CMD_FAULT : AXIS_CMD_STOPPED));
        CHECK(out.stop_applied_ticket == ticket && out.active_valid == 0U);
    }
}

static void TestTimedObserverPreventsWrapRevival(void)
{
    unsigned mode;
    for (mode = 0U; mode < 2U; ++mode)
    {
        AxisCommandFrame f;
        AxisCommandSnapshot before, after;
        uint32_t seq, next_seq;
        unsigned calls, locked, safety_before;
        Reset(); clock_tick = 100U;
        calls = lease_calls; locked = disables;
        AxisCommandPort_ObserveTime();
        CHECK(lease_calls == calls && disables == locked);
        f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
        CHECK(AxisCommandPort_SubmitTimed(&f, 100U, 100U, &seq) == AXIS_CMD_OK);
        if (mode == 0U) RequestIrq();
        before = Snapshot(); safety_before = safety_calls;
        clock_tick = 200U; /* Real time expired; TIM1 and HAL tick now stop. */
        host_ipsr = 25U; calls = lease_calls;
        AxisCommandPort_ObserveTime(); CHECK(lease_calls == calls);
        host_ipsr = 0U; current_thread = &wrong_token;
        AxisCommandPort_ObserveTime(); CHECK(lease_calls == calls);
        current_thread = appTaskHandle;
        AxisCommandPort_ObserveTime(); CHECK(lease_calls > calls);
        CHECK(safety_calls == safety_before); /* Task must not touch outputs/core. */
        clock_tick = 100U; /* A complete raw16 revolution makes modulo age zero. */
        after = Snapshot();
        CHECK(after.active_valid == before.active_valid && after.run_epoch == before.run_epoch);
        CHECK(after.applied_seq == before.applied_seq && after.permit == 0U);
        RequestIrq(); after = Snapshot();
        CHECK(after.active_valid == 0U && after.result == AXIS_CMD_EXPIRED && after.run_epoch == 2U);
        CHECK(Receipt().result == (mode ? AXIS_CMD_EXPIRED : AXIS_CMD_APPLIED));
        /* Later sequence may be used after the old epoch was revoked; the
         * watermark is not cleared or allowed to poison a new sequence. */
        f = Frame(2U, AXIS_CMD_START, 200.0f, 1.0f, 500U);
        CHECK(AxisCommandPort_SubmitTimed(&f, 100U, 100U, &next_seq) == AXIS_CMD_OK);
        CHECK(next_seq > seq); RequestIrq();
        CHECK(Snapshot().active_valid == 1U && Snapshot().permit == 1U);
    }
}

static void TestTimedMetadataLifecycle(void)
{
    AxisCommandFrame f;
    AxisCommandSnapshot out;
    AxisCommandReceipt receipt;
    uint32_t seq, other_seq, ticket;
    unsigned calls, mode;
    /* A BUSY rejected replacement must preserve the occupied pending lease. */
    Reset(); clock_tick = 100U; f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
    CHECK(AxisCommandPort_SubmitTimed(&f, 100U, 10U, &seq) == AXIS_CMD_OK);
    CHECK(AxisCommandPort_SubmitTimed(&f, 100U, 1000U, &other_seq) == AXIS_CMD_BUSY && other_seq == 0U);
    clock_tick = 110U; RequestIrq(); receipt = Receipt();
    CHECK(receipt.command_seq == seq && receipt.result == AXIS_CMD_EXPIRED);
    /* A processed/rejected START must not replace an older active timed lease. */
    Reset(); f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
    CHECK(AxisCommandPort_SubmitTimed(&f, 0U, 10U, &seq) == AXIS_CMD_OK); RequestIrq();
    CHECK(AxisCommandPort_SubmitTimed(&f, 0U, 1000U, &other_seq) == AXIS_CMD_OK); RequestIrq();
    CHECK(Receipt().result == AXIS_CMD_ALREADY_RUNNING);
    clock_tick = 10U; RequestIrq(); CHECK(Snapshot().active_valid == 0U);
    /* Applied legacy UPDATE explicitly replaces the timed sidecar. */
    Reset(); f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
    CHECK(AxisCommandPort_SubmitTimed(&f, 0U, 10U, &seq) == AXIS_CMD_OK); RequestIrq();
    f = Frame(1U, AXIS_CMD_UPDATE, 200.0f, 2.0f, 500U);
    CHECK(AxisCommandPort_Submit(&f, &other_seq) == AXIS_CMD_OK); RequestIrq();
    calls = lease_calls; clock_trusted = 0U; RequestIrq(); out = Snapshot();
    CHECK(out.active_valid == 1U && out.active.command_seq == other_seq && out.permit == 1U);
    CHECK(lease_calls == calls);
    /* Reject/STOP/explicit revoke/expiry each clear sidecars before a later
     * untimed producer. This legacy API is not the CAN transaction path. */
    for (mode = 0U; mode < 4U; ++mode)
    {
        Reset(); f = Frame(1U, AXIS_CMD_START, 100.0f, 1.0f, 500U);
        CHECK(AxisCommandPort_SubmitTimed(&f, 0U, 10U, &seq) == AXIS_CMD_OK);
        host_ipsr = 25U;
        if (mode == 0U) (void)AxisCommandPort_BeginCycle(host_now, 0U);
        else if (mode == 1U)
        {
            host_ipsr = 0U; CHECK(AxisCommandPort_Stop(&ticket) == AXIS_CMD_OK); host_ipsr = 25U;
            (void)AxisCommandPort_BeginCycle(host_now, 1U);
        }
        else if (mode == 2U) AxisCommandPort_RejectActive(AXIS_CMD_NOT_READY);
        else { clock_tick = 10U; (void)AxisCommandPort_BeginCycle(host_now, 1U); }
        host_ipsr = 0U;
        f = Frame(AxisCommandPort_GetEpoch(), AXIS_CMD_START, 100.0f, 1.0f, 500U);
        CHECK(AxisCommandPort_Submit(&f, &other_seq) == AXIS_CMD_OK);
        calls = lease_calls; clock_trusted = 0U; RequestIrq(); out = Snapshot();
        CHECK(out.active_valid == 1U && out.active.command_seq == other_seq && out.permit == 1U);
        CHECK(lease_calls == calls);
    }
}
#endif

int main(void)
{
    TestContextsAndMask();
    TestTimedContextAndProfile();
    TestReceiptContextsAndMask();
    TestTargetPrecheck();
#if AXIS_COMMAND_BENCH_ENABLE
    TestRealPublicationAndSnapshot();
    TestTimedAtomicAndRawDeadline();
    TestTimedPublishRecheck();
    TestTimedActiveAndPendingExpiry();
    TestTimedFinalGateAndPause();
    TestTimedStopAndFaultPrecedence();
    TestTimedObserverPreventsWrapRevival();
    TestTimedMetadataLifecycle();
    TestReceiptAppliedAndReplaced();
    TestReceiptActiveLoss();
    TestReceiptRejectedAndRetained();
    TestReceiptPendingCancellation();
    TestReceiptDeferredInterrupt();
    TestFaultsAndExpiry();
    TestPrecheckCannotPublishOrRenew();
#else
    TestNormalProfile();
    TestReceiptNormalProfile();
#endif
    printf("PASS: %u real command port assertions (BENCH=%u); deterministic CPU stubs only\n",
           assertions, (unsigned)AXIS_COMMAND_BENCH_ENABLE);
    return 0;
}
