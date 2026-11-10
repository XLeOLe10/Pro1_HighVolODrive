#ifndef TELEMETRY_FORMAT_H
#define TELEMETRY_FORMAT_H

#include <stddef.h>

/* At most two signed 7-digit whole parts, '.ddd', comma, CRLF and NUL. */
#define TELEMETRY_SPEED_CSV_CAPACITY 28U

/* Bounded binary32 -> decimal CSV, without printf, allocation or FP arithmetic.
 * Round to the nearest 0.001 rpm, ties to even; preserve the sign of zero.
 * Return bytes before NUL. Return 0 with dst unchanged for insufficient capacity,
 * NULL, non-finite input or magnitude rounded beyond UINT32_MAX millirpm.
 * This encoding range is NOT a motor operating limit. Caller owns dst until TX
 * takes ownership; this function must never modify an in-flight UART buffer. */
size_t TelemetryFormat_SpeedCsv(char *dst, size_t capacity,
                              float reference_rpm, float feedback_rpm);

#endif
