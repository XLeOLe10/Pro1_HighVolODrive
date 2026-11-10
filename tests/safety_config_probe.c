#include "board_config.h"
#ifndef EXPECT_BENCH
#error "Test must declare EXPECT_BENCH."
#endif
#ifndef EXPECT_POWER
#error "Test must declare EXPECT_POWER."
#endif
#if AXIS_COMMAND_BENCH_ENABLE != EXPECT_BENCH
#error "Unexpected bench definition."
#endif
#if BOARD_POWER_STAGE_MODE != EXPECT_POWER
#error "Unexpected power profile definition."
#endif
int main(void) { return 0; }
