/**
 * test_reading_store — Unity suite for the pure reading_store_core ring
 * buffer. Runs entirely on the host ([env:native]); no FreeRTOS, no board.
 *
 * Deliberately NOT tested here: clamping `points` to [1, POINTS_MAX=500] —
 * that is the HTTP layer's job in a later phase, not this module's.
 */
#include <stdbool.h>

#include <unity.h>

#include "reading_store_core.h"

static reading_store_core_t store;

void setUp(void) {
    reading_store_core_init(&store);
}

void tearDown(void) {}

static sensor_reading_t make_reading(uint32_t epoch_sec, float lux, float temp_c,
                                      level_state_t level, uint8_t valid) {
    sensor_reading_t r;
    r.epoch_sec = epoch_sec;
    r.lux = lux;
    r.temp_c = temp_c;
    r.level = level;
    r.valid = valid;
    return r;
}

/* ---- push / count / empty / full ---- */

void test_push_into_empty_ring(void) {
    TEST_ASSERT_TRUE(reading_store_core_is_empty(&store));
    sensor_reading_t r = make_reading(30, 123.4f, 21.5f, LEVEL_MID, 0x03);
    reading_store_core_push(&store, &r);

    TEST_ASSERT_FALSE(reading_store_core_is_empty(&store));
    TEST_ASSERT_EQUAL_UINT16(1, reading_store_core_count(&store));

    sensor_reading_t out[1];
    uint16_t written = reading_store_core_downsample(&store, out, 1);
    TEST_ASSERT_EQUAL_UINT16(1, written);
    TEST_ASSERT_EQUAL_UINT32(30, out[0].epoch_sec);
    TEST_ASSERT_EQUAL_FLOAT(123.4f, out[0].lux);
    TEST_ASSERT_EQUAL_FLOAT(21.5f, out[0].temp_c);
    TEST_ASSERT_EQUAL(LEVEL_MID, out[0].level);
    TEST_ASSERT_EQUAL_UINT8(0x03, out[0].valid);
}

void test_count_correct_while_partially_filled(void) {
    for (uint16_t i = 0; i < 5; i++) {
        sensor_reading_t r = make_reading(i * 30, (float)i, (float)i, LEVEL_FULL, 0x07);
        reading_store_core_push(&store, &r);
        TEST_ASSERT_EQUAL_UINT16(i + 1, reading_store_core_count(&store));
    }
    TEST_ASSERT_FALSE(reading_store_core_is_full(&store));
}

void test_fill_exactly_to_capacity(void) {
    for (uint16_t i = 0; i < READING_STORE_CORE_CAPACITY; i++) {
        sensor_reading_t r = make_reading(i, 0.0f, 0.0f, LEVEL_LOW, 0x01);
        reading_store_core_push(&store, &r);
    }
    TEST_ASSERT_EQUAL_UINT16(READING_STORE_CORE_CAPACITY, reading_store_core_count(&store));
    TEST_ASSERT_TRUE(reading_store_core_is_full(&store));
    TEST_ASSERT_FALSE(reading_store_core_is_empty(&store));
}

void test_wrap_overwrites_oldest_entry(void) {
    /* Fill to capacity with epoch_sec == index, then push one more. The
     * oldest entry (epoch_sec == 0) must be evicted; count stays at
     * capacity, and the new oldest entry is epoch_sec == 1. */
    for (uint16_t i = 0; i < READING_STORE_CORE_CAPACITY; i++) {
        sensor_reading_t r = make_reading(i, 0.0f, 0.0f, LEVEL_UNKNOWN, 0);
        reading_store_core_push(&store, &r);
    }
    sensor_reading_t overwrite = make_reading(99999, 1.0f, 1.0f, LEVEL_FULL, 0x07);
    reading_store_core_push(&store, &overwrite);

    TEST_ASSERT_EQUAL_UINT16(READING_STORE_CORE_CAPACITY, reading_store_core_count(&store));

    sensor_reading_t out[READING_STORE_CORE_CAPACITY];
    uint16_t written = reading_store_core_downsample(&store, out, READING_STORE_CORE_CAPACITY);
    TEST_ASSERT_EQUAL_UINT16(READING_STORE_CORE_CAPACITY, written);
    /* Oldest surviving entry must be epoch_sec == 1, not 0. */
    TEST_ASSERT_EQUAL_UINT32(1, out[0].epoch_sec);
    /* Newest entry must be the just-pushed overwrite. */
    TEST_ASSERT_EQUAL_UINT32(99999, out[READING_STORE_CORE_CAPACITY - 1].epoch_sec);
}

/* ---- downsample ---- */

void test_downsample_2880_to_180_evenly(void) {
    for (uint16_t i = 0; i < READING_STORE_CORE_CAPACITY; i++) {
        sensor_reading_t r = make_reading(i, 0.0f, 0.0f, LEVEL_UNKNOWN, 0);
        reading_store_core_push(&store, &r);
    }

    sensor_reading_t out[180];
    uint16_t written = reading_store_core_downsample(&store, out, 180);
    TEST_ASSERT_EQUAL_UINT16(180, written);

    /* Must span oldest to newest: first sample near the start, last sample
     * the newest one, and monotonically increasing epoch_sec throughout. */
    TEST_ASSERT_EQUAL_UINT32(0, out[0].epoch_sec);
    TEST_ASSERT_EQUAL_UINT32(READING_STORE_CORE_CAPACITY - 1, out[179].epoch_sec);
    for (uint16_t i = 1; i < 180; i++) {
        TEST_ASSERT_TRUE(out[i].epoch_sec > out[i - 1].epoch_sec);
    }
}

void test_downsample_fewer_samples_than_requested_points(void) {
    for (uint16_t i = 0; i < 10; i++) {
        sensor_reading_t r = make_reading(i, 0.0f, 0.0f, LEVEL_LOW, 0x01);
        reading_store_core_push(&store, &r);
    }

    sensor_reading_t out[50];
    uint16_t written = reading_store_core_downsample(&store, out, 50);
    /* Only 10 entries exist; must not fabricate the other 40. */
    TEST_ASSERT_EQUAL_UINT16(10, written);
    for (uint16_t i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_UINT32(i, out[i].epoch_sec);
    }
}

void test_downsample_more_points_than_ring_capacity(void) {
    for (uint16_t i = 0; i < READING_STORE_CORE_CAPACITY; i++) {
        sensor_reading_t r = make_reading(i, 0.0f, 0.0f, LEVEL_UNKNOWN, 0);
        reading_store_core_push(&store, &r);
    }

    sensor_reading_t out[READING_STORE_CORE_CAPACITY];
    /* Request far more points than the ring holds; must not read/write past
     * the ring's current count. */
    uint16_t written = reading_store_core_downsample(&store, out, 5000);
    TEST_ASSERT_EQUAL_UINT16(READING_STORE_CORE_CAPACITY, written);
}

void test_downsample_from_empty_ring_returns_zero(void) {
    sensor_reading_t out[10];
    uint16_t written = reading_store_core_downsample(&store, out, 10);
    TEST_ASSERT_EQUAL_UINT16(0, written);
}

void test_downsample_to_exactly_one_point(void) {
    for (uint16_t i = 0; i < 100; i++) {
        sensor_reading_t r = make_reading(i, 0.0f, 0.0f, LEVEL_LOW, 0x01);
        reading_store_core_push(&store, &r);
    }

    sensor_reading_t out[1];
    uint16_t written = reading_store_core_downsample(&store, out, 1);
    TEST_ASSERT_EQUAL_UINT16(1, written);
    /* A single requested point should be the newest sample. */
    TEST_ASSERT_EQUAL_UINT32(99, out[0].epoch_sec);
}

void test_downsample_to_exact_current_count(void) {
    for (uint16_t i = 0; i < 25; i++) {
        sensor_reading_t r = make_reading(i, 0.0f, 0.0f, LEVEL_MID, 0x03);
        reading_store_core_push(&store, &r);
    }

    sensor_reading_t out[25];
    uint16_t written = reading_store_core_downsample(&store, out, 25);
    TEST_ASSERT_EQUAL_UINT16(25, written);
    for (uint16_t i = 0; i < 25; i++) {
        TEST_ASSERT_EQUAL_UINT32(i, out[i].epoch_sec);
    }
}

void test_downsample_zero_points_writes_nothing(void) {
    sensor_reading_t r = make_reading(1, 1.0f, 1.0f, LEVEL_FULL, 0x07);
    reading_store_core_push(&store, &r);

    sensor_reading_t out[1];
    uint16_t written = reading_store_core_downsample(&store, out, 0);
    TEST_ASSERT_EQUAL_UINT16(0, written);
}

/* ---- time_valid bit (Phase 5) ---- */

void test_time_valid_bit_survives_push_and_downsample(void) {
    /* One entry with a synced (valid) epoch, one still unsynced (bit
     * clear) — downsample is a plain struct copy, so both entries' full
     * `valid` bitfield (light/temp/level + time) must come through the ring
     * unchanged, not just the pre-existing three sensor bits. */
    sensor_reading_t synced = make_reading(1700000000, 10.0f, 20.0f, LEVEL_FULL,
                                            (uint8_t)(READING_VALID_LIGHT_BIT | READING_VALID_TEMP_BIT |
                                                       READING_VALID_LEVEL_BIT | READING_VALID_TIME_BIT));
    sensor_reading_t unsynced = make_reading(5, 11.0f, 21.0f, LEVEL_FULL,
                                              (uint8_t)(READING_VALID_LIGHT_BIT | READING_VALID_TEMP_BIT |
                                                         READING_VALID_LEVEL_BIT));
    reading_store_core_push(&store, &synced);
    reading_store_core_push(&store, &unsynced);

    sensor_reading_t out[2];
    uint16_t written = reading_store_core_downsample(&store, out, 2);
    TEST_ASSERT_EQUAL_UINT16(2, written);

    TEST_ASSERT_TRUE((out[0].valid & READING_VALID_TIME_BIT) != 0);
    TEST_ASSERT_EQUAL_UINT32(1700000000, out[0].epoch_sec);

    TEST_ASSERT_FALSE((out[1].valid & READING_VALID_TIME_BIT) != 0);
    /* The sensor bits on the unsynced entry must be untouched by the
     * time bit being clear — the two are independent. */
    TEST_ASSERT_TRUE((out[1].valid & READING_VALID_LIGHT_BIT) != 0);
    TEST_ASSERT_TRUE((out[1].valid & READING_VALID_TEMP_BIT) != 0);
    TEST_ASSERT_TRUE((out[1].valid & READING_VALID_LEVEL_BIT) != 0);
}

void test_mixed_valid_invalid_time_entries_through_downsample(void) {
    /* Push 10 entries, alternating time_valid on even/odd index, and
     * downsample to exactly the current count (a plain full copy, per
     * test_downsample_to_exact_current_count above) — every entry's
     * time_valid bit must match what was pushed, index for index. */
    for (uint16_t i = 0; i < 10; i++) {
        uint8_t valid = READING_VALID_LIGHT_BIT;
        if ((i % 2) == 0) {
            valid = (uint8_t)(valid | READING_VALID_TIME_BIT);
        }
        sensor_reading_t r = make_reading(i, 0.0f, 0.0f, LEVEL_LOW, valid);
        reading_store_core_push(&store, &r);
    }

    sensor_reading_t out[10];
    uint16_t written = reading_store_core_downsample(&store, out, 10);
    TEST_ASSERT_EQUAL_UINT16(10, written);

    for (uint16_t i = 0; i < 10; i++) {
        bool expect_time_valid = (i % 2) == 0;
        bool actual_time_valid = (out[i].valid & READING_VALID_TIME_BIT) != 0;
        TEST_ASSERT_EQUAL(expect_time_valid, actual_time_valid);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_push_into_empty_ring);
    RUN_TEST(test_count_correct_while_partially_filled);
    RUN_TEST(test_fill_exactly_to_capacity);
    RUN_TEST(test_wrap_overwrites_oldest_entry);
    RUN_TEST(test_downsample_2880_to_180_evenly);
    RUN_TEST(test_downsample_fewer_samples_than_requested_points);
    RUN_TEST(test_downsample_more_points_than_ring_capacity);
    RUN_TEST(test_downsample_from_empty_ring_returns_zero);
    RUN_TEST(test_downsample_to_exactly_one_point);
    RUN_TEST(test_downsample_to_exact_current_count);
    RUN_TEST(test_downsample_zero_points_writes_nothing);
    RUN_TEST(test_time_valid_bit_survives_push_and_downsample);
    RUN_TEST(test_mixed_valid_invalid_time_entries_through_downsample);
    return UNITY_END();
}
