/**
 * test_wifi_backoff — Unity suite for the pure capped exponential-backoff
 * delay sequencer. Runs entirely on the host ([env:native]); no FreeRTOS, no
 * board, no Wi-Fi/esp_event calls.
 *
 * Deliberately NOT tested here: actually sleeping for the returned delay,
 * scheduling the reconnect timer/task, or anything Wi-Fi-specific — those
 * are src/wifi_conn.c's job (device-only, not host-testable per the Test
 * Strategy) and require a real AP to exercise meaningfully.
 */
#include <unity.h>

#include "wifi_backoff.h"

static wifi_backoff_t b;

void setUp(void) {
    wifi_backoff_reset(&b);
}

void tearDown(void) {}

/* ---- first call after reset ---- */

void test_first_call_after_reset_returns_floor_of_one_second(void) {
    TEST_ASSERT_EQUAL_UINT32(1, wifi_backoff_next_delay_sec(&b));
}

/* ---- doubling sequence, capped at 30 (per AC-ERROR-3: 1->2->4->8->16->30) ---- */

void test_doubling_sequence_caps_at_thirty_not_thirty_two(void) {
    uint32_t expected[] = {1, 2, 4, 8, 16, 30};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        TEST_ASSERT_EQUAL_UINT32(expected[i], wifi_backoff_next_delay_sec(&b));
    }
}

/* ---- stays capped on repeated calls past the cap ---- */

void test_delay_stays_capped_at_thirty_on_repeated_calls(void) {
    for (int i = 0; i < 6; i++) {
        wifi_backoff_next_delay_sec(&b); /* drive through 1,2,4,8,16,30 */
    }
    TEST_ASSERT_EQUAL_UINT32(30, wifi_backoff_next_delay_sec(&b));
    TEST_ASSERT_EQUAL_UINT32(30, wifi_backoff_next_delay_sec(&b));
    TEST_ASSERT_EQUAL_UINT32(30, wifi_backoff_next_delay_sec(&b));
}

/* ---- reset restores the sequence to the floor after reaching the cap ---- */

void test_reset_restores_sequence_to_floor_after_cap(void) {
    for (int i = 0; i < 8; i++) {
        wifi_backoff_next_delay_sec(&b); /* well past the cap */
    }
    wifi_backoff_reset(&b);
    TEST_ASSERT_EQUAL_UINT32(1, wifi_backoff_next_delay_sec(&b));
    TEST_ASSERT_EQUAL_UINT32(2, wifi_backoff_next_delay_sec(&b));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_first_call_after_reset_returns_floor_of_one_second);
    RUN_TEST(test_doubling_sequence_caps_at_thirty_not_thirty_two);
    RUN_TEST(test_delay_stays_capped_at_thirty_on_repeated_calls);
    RUN_TEST(test_reset_restores_sequence_to_floor_after_cap);
    return UNITY_END();
}
