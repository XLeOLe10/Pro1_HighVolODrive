#include "telemetry_format.h"

#include <fenv.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

static unsigned long checks;
static unsigned long oracle_cases;
static unsigned long oracle_mismatches;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static float from_bits(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/* Exercise every capacity around each frame's actual required size. */
static void expect_frame(float reference, float feedback, const char *expected)
{
    unsigned char guarded[96];
    unsigned char before[sizeof(guarded)];
    const size_t prefix = 16U;
    const size_t length = strlen(expected);
    size_t capacity;
    CHECK(length < TELEMETRY_SPEED_CSV_CAPACITY);
    for (capacity = 0U; capacity <= length + 3U; ++capacity) {
        size_t result;
        memset(guarded, 0xa5, sizeof(guarded));
        memcpy(before, guarded, sizeof(guarded));
        result = TelemetryFormat_SpeedCsv((char *)guarded + prefix, capacity,
                                         reference, feedback);
        if (capacity <= length) {
            CHECK(result == 0U);
            CHECK(memcmp(guarded, before, sizeof(guarded)) == 0);
        } else {
            CHECK(result == length);
            CHECK(memcmp(guarded + prefix, expected, length + 1U) == 0);
            CHECK(memcmp(guarded, before, prefix) == 0);
            CHECK(memcmp(guarded + prefix + length + 1U,
                         before + prefix + length + 1U,
                         sizeof(guarded) - prefix - length - 1U) == 0);
        }
    }
}

static void expect_failure(float reference, float feedback)
{
    unsigned char guarded[96];
    unsigned char before[sizeof(guarded)];
    const size_t capacities[] = {0U, 1U, 13U, 27U, 28U, 64U};
    size_t i;
    for (i = 0U; i < sizeof(capacities) / sizeof(capacities[0]); ++i) {
        memset(guarded, 0x5a, sizeof(guarded));
        memcpy(before, guarded, sizeof(guarded));
        CHECK(TelemetryFormat_SpeedCsv((char *)guarded + 16U, capacities[i],
                                      reference, feedback) == 0U);
        CHECK(memcmp(guarded, before, sizeof(guarded)) == 0);
    }
}

static void fixed_cases(void)
{
    static const uint32_t invalid_bits[] = {
        UINT32_C(0x7f800000), UINT32_C(0xff800000), /* +/- infinity */
        UINT32_C(0x7fc00000), UINT32_C(0xffc00000), /* quiet NaNs */
        UINT32_C(0x7f800001), UINT32_C(0xff800001), /* signaling NaNs */
        UINT32_C(0x7fffffff), UINT32_C(0xffffffff), /* NaN payloads */
        UINT32_C(0x7f7fffff), UINT32_C(0xff7fffff)  /* +/- FLT_MAX */
    };
    size_t i;
    CHECK(TELEMETRY_SPEED_CSV_CAPACITY == 28U);
    CHECK(TelemetryFormat_SpeedCsv(NULL, 0U, 0.0f, 0.0f) == 0U);
    CHECK(TelemetryFormat_SpeedCsv(NULL, 28U, 1.0f, -1.0f) == 0U);
    expect_frame(0.0f, 0.0f, "0.000,0.000\r\n");
    expect_frame(0.0f, from_bits(UINT32_C(0x80000000)), "0.000,-0.000\r\n");
    expect_frame(from_bits(UINT32_C(0x80000000)), 0.0f, "-0.000,0.000\r\n");
    expect_frame(-0.0f, -0.0f, "-0.000,-0.000\r\n");
    expect_frame(1234.567f, -2345.678f, "1234.567,-2345.678\r\n");
    expect_frame(1.2345f, -1.2345f, "1.235,-1.235\r\n");
    expect_frame(1.0f, -2.0f, "1.000,-2.000\r\n");
    expect_frame(0.9999f, -9.9999f, "1.000,-10.000\r\n");
    expect_frame(99.9999f, -999.9999f, "100.000,-1000.000\r\n");
    expect_frame(0.9995f, -0.9995f, "0.999,-0.999\r\n");
    expect_frame(0.0001f, -0.0001f, "0.000,-0.000\r\n");
    expect_frame(from_bits(UINT32_C(0x00000001)), from_bits(UINT32_C(0x80000001)),
                 "0.000,-0.000\r\n");
    expect_frame(from_bits(UINT32_C(0x007fffff)), from_bits(UINT32_C(0x807fffff)),
                 "0.000,-0.000\r\n");
    expect_frame(FLT_MIN, -FLT_MIN, "0.000,-0.000\r\n");
    /* Actual binary32 values immediately below and above 0.0005. */
    expect_frame(from_bits(UINT32_C(0x3a03126e)), from_bits(UINT32_C(0x3a03126f)),
                 "0.000,0.001\r\n");
    expect_frame(from_bits(UINT32_C(0xba03126e)), from_bits(UINT32_C(0xba03126f)),
                 "-0.000,-0.001\r\n");
    /* Exact representable ties, plus each tie's adjacent binary32 inputs. */
    expect_frame(0.0625f, 0.1875f, "0.062,0.188\r\n");
    expect_frame(-0.0625f, -0.1875f, "-0.062,-0.188\r\n");
    expect_frame(0.3125f, 0.4375f, "0.312,0.438\r\n");
    expect_frame(from_bits(UINT32_C(0x3d7fffff)), from_bits(UINT32_C(0x3d800001)),
                 "0.062,0.063\r\n");
    expect_frame(from_bits(UINT32_C(0x3e3fffff)), from_bits(UINT32_C(0x3e400001)),
                 "0.187,0.188\r\n");
    expect_frame(4294967.0f, -4294967.0f, "4294967.000,-4294967.000\r\n");
    expect_frame(-4294967.0f, -4294967.0f, "-4294967.000,-4294967.000\r\n");
    expect_frame(4294966.5f, -4294966.5f, "4294966.500,-4294966.500\r\n");
    expect_failure(4294967.5f, 0.0f);
    expect_failure(0.0f, 4294967.5f);
    expect_failure(-4294967.5f, 0.0f);
    expect_failure(0.0f, -4294967.5f);
    expect_failure(8388608.0f, -8388608.0f);
    expect_failure(1.0e20f, 0.0f);
    for (i = 0U; i < sizeof(invalid_bits) / sizeof(invalid_bits[0]); ++i) {
        expect_failure(from_bits(invalid_bits[i]), 7.0f);
        expect_failure(7.0f, from_bits(invalid_bits[i]));
    }
}

static uint32_t random_state = UINT32_C(0x6310f03d);

static uint32_t random_u32(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static float random_valid_binary32(void)
{
    uint32_t bits;
    /* 4294967.0 is the largest accepted binary32; next value is 4294967.5. */
    const uint32_t largest = float_bits(4294967.0f);
    do {
        bits = random_u32();
    } while ((bits & UINT32_C(0x7fffffff)) > largest);
    return from_bits(bits);
}

/* libc is an independent decimal conversion oracle, not a copy of the helper. */
static void compare_snprintf(float reference, float feedback)
{
    unsigned char guarded[64];
    char expected[64];
    size_t actual_length;
    size_t i;
    int expected_length;
    memset(guarded, 0x3c, sizeof(guarded));
    expected_length = snprintf(expected, sizeof(expected), "%.3f,%.3f\r\n",
                               (double)reference, (double)feedback);
    CHECK(expected_length > 0 && (size_t)expected_length < sizeof(expected));
    actual_length = TelemetryFormat_SpeedCsv((char *)guarded + 8U,
                                            TELEMETRY_SPEED_CSV_CAPACITY,
                                            reference, feedback);
    CHECK(actual_length > 0U && actual_length < TELEMETRY_SPEED_CSV_CAPACITY);
    CHECK(guarded[8U + actual_length] == '\0');
    for (i = 0U; i < 8U; ++i) CHECK(guarded[i] == 0x3c);
    for (i = 8U + actual_length + 1U; i < sizeof(guarded); ++i)
        CHECK(guarded[i] == 0x3c);
    ++oracle_cases;
    if (actual_length != (size_t)expected_length ||
        memcmp(guarded + 8U, expected, (size_t)expected_length + 1U) != 0) {
        ++oracle_mismatches;
        if (oracle_mismatches <= 8U) {
            fprintf(stderr, "snprintf mismatch ref=0x%08lx feedback=0x%08lx\n"
                    "  production: %s  libc: %s",
                    (unsigned long)float_bits(reference),
                    (unsigned long)float_bits(feedback),
                    (const char *)guarded + 8U, expected);
        }
    }
}

static void oracle_tests(void)
{
    unsigned long i;
    /* Explicit ties reveal any host-libc tie convention difference. */
    compare_snprintf(0.0625f, 0.1875f);
    compare_snprintf(-0.0625f, -0.1875f);
    compare_snprintf(0.3125f, 0.4375f);
    compare_snprintf(-0.0f, 0.0f);
    compare_snprintf(-4294967.0f, 4294967.0f);
    for (i = 0U; i < 150000U; ++i) {
        float reference = random_valid_binary32();
        float feedback = random_valid_binary32();
        compare_snprintf(reference, feedback);
    }
    for (i = 0U; i < 50000U; ++i) {
        /* Additional densely sampled, ordinary signed RPM inputs. */
        float reference = ((float)(random_u32() % 200000001U) - 100000000.0f) / 1000.0f;
        float feedback = ((float)(random_u32() % 200000001U) - 100000000.0f) / 1000.0f;
        compare_snprintf(reference, feedback);
    }
    if (oracle_mismatches != 0U) {
        fprintf(stderr, "FAIL: %lu / %lu snprintf comparisons differ.\n"
                "Inspect host libc rounding before attributing differences to production.\n",
                oracle_mismatches, oracle_cases);
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    CHECK(sizeof(float) == sizeof(uint32_t));
    CHECK(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128);
    CHECK(setlocale(LC_NUMERIC, "C") != NULL);
    CHECK(fesetround(FE_TONEAREST) == 0);
    fixed_cases();
    oracle_tests();
    /* Integer formatting must remain stable under other host FP modes. */
    CHECK(fesetround(FE_UPWARD) == 0);
    expect_frame(0.0625f, -0.1875f, "0.062,-0.188\r\n");
    CHECK(fesetround(FE_DOWNWARD) == 0);
    expect_frame(0.1875f, -0.0625f, "0.188,-0.062\r\n");
    CHECK(fesetround(FE_TOWARDZERO) == 0);
    expect_frame(0.1875f, -0.1875f, "0.188,-0.188\r\n");
    CHECK(fesetround(FE_TONEAREST) == 0);
    printf("PASS: %lu assertions; %lu independent snprintf comparisons; "
           "all capacity guards and failure buffers intact.\n", checks, oracle_cases);
    return EXIT_SUCCESS;
}
