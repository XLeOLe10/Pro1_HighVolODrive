#include "telemetry_format.h"
#include <float.h>
#include <stdint.h>
#include <string.h>

#if (FLT_RADIX != 2) || (FLT_MANT_DIG != 24) || (FLT_MAX_EXP != 128)
#error "Telemetry formatter requires IEEE-754 binary32 float."
#endif
typedef char TelemetryFloatMustBe32Bits[(sizeof(float) == sizeof(uint32_t)) ? 1 : -1];

typedef struct {
    uint32_t milli;
    uint32_t negative;
} DecimalSpeed;

static int ToDecimalSpeed(float value, DecimalSpeed *out)
{
    uint32_t bits;
    uint32_t exponent;
    uint32_t mantissa;
    uint32_t shift;
    uint64_t scaled;
    uint64_t whole;
    uint64_t remainder;
    uint64_t half;

    /* memcpy avoids type-punning aliasing and also preserves negative zero. */
    memcpy(&bits, &value, sizeof(bits));
    exponent = (bits >> 23U) & 0xffU;
    if (exponent == 0xffU) return 0; /* NaN/Inf: do not send a numeric lie. */
    out->negative = bits >> 31U;
    if (exponent == 0U)
    {
        out->milli = 0U; /* Zero/subnormal: all magnitudes round to zero. */
        return 1;
    }
    /* exponent >= 150 implies at least 2^23 rpm, outside the encoding range. */
    if (exponent >= 150U) return 0;
    shift = 150U - exponent;
    if (shift >= 35U)
    {
        out->milli = 0U;
        return 1;
    }

    mantissa = (bits & 0x7fffffU) | 0x800000U;
    scaled = (uint64_t)mantissa * 1000U; /* Exact, less than 2^34. */
    whole = scaled >> shift;
    remainder = scaled & (((uint64_t)1U << shift) - 1U);
    half = (uint64_t)1U << (shift - 1U);
    if ((remainder > half) || ((remainder == half) && ((whole & 1U) != 0U)))
        whole++;
    if (whole > UINT32_MAX) return 0;
    out->milli = (uint32_t)whole;
    return 1;
}

static size_t WholeDigits(uint32_t value)
{
    size_t count = 1U;
    while (value >= 10U) /* At most seven digits for this encoding. */
    {
        value /= 10U;
        count++;
    }
    return count;
}

static char *AppendSpeed(char *dst, const DecimalSpeed *value)
{
    char reverse[7];
    uint32_t whole = value->milli / 1000U;
    uint32_t fraction = value->milli % 1000U;
    size_t used = 0U;
    if (value->negative != 0U) *dst++ = '-';
    do {
        reverse[used++] = (char)('0' + (whole % 10U));
        whole /= 10U;
    } while (whole != 0U);
    while (used != 0U) *dst++ = reverse[--used];
    *dst++ = '.';
    *dst++ = (char)('0' + (fraction / 100U));
    *dst++ = (char)('0' + ((fraction / 10U) % 10U));
    *dst++ = (char)('0' + (fraction % 10U));
    return dst;
}

size_t TelemetryFormat_SpeedCsv(char *dst, size_t capacity,
                              float reference_rpm, float feedback_rpm)
{
    DecimalSpeed reference;
    DecimalSpeed feedback;
    size_t length;
    char *end;
    if ((dst == NULL) ||
        !ToDecimalSpeed(reference_rpm, &reference) ||
        !ToDecimalSpeed(feedback_rpm, &feedback)) return 0U;
    length = WholeDigits(reference.milli / 1000U) + reference.negative + 4U +
             WholeDigits(feedback.milli / 1000U) + feedback.negative + 4U + 3U;
    if (capacity <= length) return 0U;
    /* Validation and capacity checks finish before the first output write. */
    end = AppendSpeed(dst, &reference);
    *end++ = ',';
    end = AppendSpeed(end, &feedback);
    *end++ = '\r';
    *end++ = '\n';
    *end = '\0';
    return length;
}
