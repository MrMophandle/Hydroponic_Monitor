/**
 * test_sensor_hub — Unity suite for sensor_hub's per-cycle orchestration and
 * consecutive-failure escalation. Runs entirely on the host ([env:native]);
 * the three physical sensor reads are replaced at link time by the
 * controllable stubs in test/native/stubs/ (see sensor_hub_stubs.h) — no
 * FreeRTOS, no board, no I2C/1-Wire/GPIO access.
 *
 * Deliberately NOT tested here: the level_switches debounce state machine
 * itself (already exercised by test/test_level_switches/) and the physical
 * GPIO/I2C/1-Wire acquisition (device-only, bench-verified — see
 * src/sampler.c).
 */
#include <unity.h>

#include "sensor_hub.h"
#include "sensor_hub_stubs.h"

void setUp(void) {
    bh1750_stub_reset();
    ds18b20_probe_stub_reset();
    level_switches_stub_reset();
}

void tearDown(void) {}

/* ---- partial / total failure independence ---- */

void test_one_sensor_fails_others_still_record(void) {
    bh1750_stub_next_result = ESP_FAIL;

    ds18b20_probe_stub_next_result = ESP_OK;
    ds18b20_probe_stub_next_temp_c = 21.5f;

    level_switches_stub_next_result = ESP_OK;
    level_switches_stub_next_state = LEVEL_SWITCH_STATE_MID;

    sensor_hub_t hub;
    sensor_hub_init(&hub);
    sensor_hub_reading_t reading = sensor_hub_run_cycle(&hub);

    TEST_ASSERT_FALSE(reading.light_valid);
    TEST_ASSERT_TRUE(reading.temp_valid);
    TEST_ASSERT_TRUE(reading.level_valid);
    TEST_ASSERT_EQUAL_FLOAT(21.5f, reading.temp_c);
    TEST_ASSERT_EQUAL(LEVEL_SWITCH_STATE_MID, reading.level);
}

void test_all_three_fail_cycle_completes_without_aborting(void) {
    bh1750_stub_next_result = ESP_FAIL;
    ds18b20_probe_stub_next_result = ESP_FAIL;
    level_switches_stub_next_result = ESP_FAIL;

    sensor_hub_t hub;
    sensor_hub_init(&hub);
    sensor_hub_reading_t reading = sensor_hub_run_cycle(&hub);

    TEST_ASSERT_FALSE(reading.light_valid);
    TEST_ASSERT_FALSE(reading.temp_valid);
    TEST_ASSERT_FALSE(reading.level_valid);
}

/* ---- consecutive-failure counter ---- */

void test_consecutive_failure_counter_increments_across_repeated_failures(void) {
    bh1750_stub_next_result = ESP_FAIL;

    sensor_hub_t hub;
    sensor_hub_init(&hub);

    for (uint8_t cycle = 1; cycle <= 3; cycle++) {
        sensor_hub_run_cycle(&hub);
        TEST_ASSERT_EQUAL_UINT8(cycle,
                                 sensor_hub_consecutive_failures(&hub, SENSOR_HUB_SENSOR_LIGHT));
        TEST_ASSERT_FALSE(sensor_hub_is_offline(&hub, SENSOR_HUB_SENSOR_LIGHT));
    }
}

void test_consecutive_failure_counter_resets_on_successful_read(void) {
    bh1750_stub_next_result = ESP_FAIL;

    sensor_hub_t hub;
    sensor_hub_init(&hub);
    sensor_hub_run_cycle(&hub);
    sensor_hub_run_cycle(&hub);
    TEST_ASSERT_EQUAL_UINT8(2, sensor_hub_consecutive_failures(&hub, SENSOR_HUB_SENSOR_LIGHT));

    bh1750_stub_next_result = ESP_OK;
    bh1750_stub_next_lux = 100.0f;
    sensor_hub_reading_t reading = sensor_hub_run_cycle(&hub);

    TEST_ASSERT_TRUE(reading.light_valid);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, reading.lux);
    TEST_ASSERT_EQUAL_UINT8(0, sensor_hub_consecutive_failures(&hub, SENSOR_HUB_SENSOR_LIGHT));
}

/* ---- offline escalation at exactly the fifth consecutive failure ---- */

void test_sensor_marked_offline_at_exactly_fifth_consecutive_failure(void) {
    bh1750_stub_next_result = ESP_FAIL;

    sensor_hub_t hub;
    sensor_hub_init(&hub);

    /* Failures 1-4: must NOT be offline yet. */
    for (uint8_t cycle = 1; cycle <= 4; cycle++) {
        sensor_hub_run_cycle(&hub);
        TEST_ASSERT_FALSE(sensor_hub_is_offline(&hub, SENSOR_HUB_SENSOR_LIGHT));
    }

    /* Failure 5: must become offline exactly now. */
    sensor_hub_run_cycle(&hub);
    TEST_ASSERT_TRUE(sensor_hub_is_offline(&hub, SENSOR_HUB_SENSOR_LIGHT));
    TEST_ASSERT_EQUAL_UINT8(5, sensor_hub_consecutive_failures(&hub, SENSOR_HUB_SENSOR_LIGHT));

    /* Failure 6: stays offline (not a one-cycle blip back to online). */
    sensor_hub_run_cycle(&hub);
    TEST_ASSERT_TRUE(sensor_hub_is_offline(&hub, SENSOR_HUB_SENSOR_LIGHT));
}

void test_successful_read_after_offline_clears_offline_state(void) {
    bh1750_stub_next_result = ESP_FAIL;

    sensor_hub_t hub;
    sensor_hub_init(&hub);
    for (uint8_t cycle = 0; cycle < SENSOR_HUB_OFFLINE_THRESHOLD; cycle++) {
        sensor_hub_run_cycle(&hub);
    }
    TEST_ASSERT_TRUE(sensor_hub_is_offline(&hub, SENSOR_HUB_SENSOR_LIGHT));

    bh1750_stub_next_result = ESP_OK;
    bh1750_stub_next_lux = 50.0f;
    sensor_hub_reading_t reading = sensor_hub_run_cycle(&hub);

    TEST_ASSERT_TRUE(reading.light_valid);
    TEST_ASSERT_FALSE(sensor_hub_is_offline(&hub, SENSOR_HUB_SENSOR_LIGHT));
    TEST_ASSERT_EQUAL_UINT8(0, sensor_hub_consecutive_failures(&hub, SENSOR_HUB_SENSOR_LIGHT));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_one_sensor_fails_others_still_record);
    RUN_TEST(test_all_three_fail_cycle_completes_without_aborting);
    RUN_TEST(test_consecutive_failure_counter_increments_across_repeated_failures);
    RUN_TEST(test_consecutive_failure_counter_resets_on_successful_read);
    RUN_TEST(test_sensor_marked_offline_at_exactly_fifth_consecutive_failure);
    RUN_TEST(test_successful_read_after_offline_clears_offline_state);
    return UNITY_END();
}
