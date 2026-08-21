/**
 * test_status_led_core — Unity suite for the pure onboard-status-LED policy
 * core. Runs entirely on the host ([env:native]); no FreeRTOS, no RMT
 * peripheral, no board, no Wi-Fi radio.
 *
 * Deliberately NOT tested here: actual WS2812/RMT bit-timing, the tick task
 * scheduling, GPIO selection, or Wi-Fi/HTTP event wiring — those are
 * lib/status_led/ and src/status_led_task.c's job (device-only, not
 * host-testable per the Test Strategy) and are verified by a bench procedure
 * instead (Phase 2/3).
 */
#include <unity.h>

#include "status_led_core.h"

void setUp(void) {}
void tearDown(void) {}

/* ================= State derivation: exhaustive 9-cell truth table ================= */
/* http == DOWN outranks everything (3 cells). */

void test_derive_state_wifi_unknown_http_down_is_red_solid(void) {
    status_facts_t f = {STATUS_FACT_UNKNOWN, STATUS_FACT_DOWN};
    TEST_ASSERT_EQUAL(STATUS_LED_STATE_RED_SOLID, status_led_core_derive_state(f));
}

void test_derive_state_wifi_up_http_down_is_red_solid(void) {
    status_facts_t f = {STATUS_FACT_UP, STATUS_FACT_DOWN};
    TEST_ASSERT_EQUAL(STATUS_LED_STATE_RED_SOLID, status_led_core_derive_state(f));
}

void test_derive_state_wifi_down_http_down_is_red_solid(void) {
    status_facts_t f = {STATUS_FACT_DOWN, STATUS_FACT_DOWN};
    TEST_ASSERT_EQUAL(STATUS_LED_STATE_RED_SOLID, status_led_core_derive_state(f));
}

/* wifi == UP && http == UP is the sole GREEN_SOLID cell. */

void test_derive_state_wifi_up_http_up_is_green_solid(void) {
    status_facts_t f = {STATUS_FACT_UP, STATUS_FACT_UP};
    TEST_ASSERT_EQUAL(STATUS_LED_STATE_GREEN_SOLID, status_led_core_derive_state(f));
}

/* Every other combination (5 cells) is RED_BLINK. */

void test_derive_state_wifi_unknown_http_unknown_is_red_blink(void) {
    status_facts_t f = {STATUS_FACT_UNKNOWN, STATUS_FACT_UNKNOWN};
    TEST_ASSERT_EQUAL(STATUS_LED_STATE_RED_BLINK, status_led_core_derive_state(f));
}

void test_derive_state_wifi_unknown_http_up_is_red_blink(void) {
    status_facts_t f = {STATUS_FACT_UNKNOWN, STATUS_FACT_UP};
    TEST_ASSERT_EQUAL(STATUS_LED_STATE_RED_BLINK, status_led_core_derive_state(f));
}

void test_derive_state_wifi_up_http_unknown_is_red_blink(void) {
    status_facts_t f = {STATUS_FACT_UP, STATUS_FACT_UNKNOWN};
    TEST_ASSERT_EQUAL(STATUS_LED_STATE_RED_BLINK, status_led_core_derive_state(f));
}

void test_derive_state_wifi_down_http_unknown_is_red_blink(void) {
    status_facts_t f = {STATUS_FACT_DOWN, STATUS_FACT_UNKNOWN};
    TEST_ASSERT_EQUAL(STATUS_LED_STATE_RED_BLINK, status_led_core_derive_state(f));
}

void test_derive_state_wifi_down_http_up_is_red_blink(void) {
    status_facts_t f = {STATUS_FACT_DOWN, STATUS_FACT_UP};
    TEST_ASSERT_EQUAL(STATUS_LED_STATE_RED_BLINK, status_led_core_derive_state(f));
}

/* ================= Blink phase edge cases ================= */

void test_blink_is_lit_at_tick_zero(void) {
    TEST_ASSERT_TRUE(status_led_core_blink_is_lit(0, 10));
}

void test_blink_is_dark_at_tick_equal_to_blink_ticks(void) {
    TEST_ASSERT_FALSE(status_led_core_blink_is_lit(10, 10));
}

void test_blink_is_lit_again_at_tick_equal_to_two_times_blink_ticks(void) {
    TEST_ASSERT_TRUE(status_led_core_blink_is_lit(20, 10));
}

void test_blink_with_blink_ticks_one_alternates_every_tick(void) {
    TEST_ASSERT_TRUE(status_led_core_blink_is_lit(0, 1));
    TEST_ASSERT_FALSE(status_led_core_blink_is_lit(1, 1));
    TEST_ASSERT_TRUE(status_led_core_blink_is_lit(2, 1));
    TEST_ASSERT_FALSE(status_led_core_blink_is_lit(3, 1));
}

void test_blink_with_zero_blink_ticks_is_always_lit_and_does_not_crash(void) {
    TEST_ASSERT_TRUE(status_led_core_blink_is_lit(0, 0));
    TEST_ASSERT_TRUE(status_led_core_blink_is_lit(1, 0));
    TEST_ASSERT_TRUE(status_led_core_blink_is_lit(12345, 0));
}

void test_blink_wraps_correctly_near_uint32_max(void) {
    /* blink_ticks = 10: tick UINT32_MAX (4294967295) / 10 = 429496729,
     * which is odd -> dark. The next tick (wraps to 0) / 10 = 0 -> lit. */
    TEST_ASSERT_FALSE(status_led_core_blink_is_lit(UINT32_MAX, 10));
    uint32_t wrapped = UINT32_MAX + 1u; /* wraps to 0 in uint32_t arithmetic */
    TEST_ASSERT_EQUAL_UINT32(0, wrapped);
    TEST_ASSERT_TRUE(status_led_core_blink_is_lit(wrapped, 10));
}

/* ================= Brightness scaling edge cases ================= */

void test_scale_brightness_full_passes_channel_through_unchanged(void) {
    TEST_ASSERT_EQUAL_UINT8(0, status_led_core_scale_brightness(0, 255));
    TEST_ASSERT_EQUAL_UINT8(128, status_led_core_scale_brightness(128, 255));
    TEST_ASSERT_EQUAL_UINT8(255, status_led_core_scale_brightness(255, 255));
}

void test_scale_brightness_max_channel_max_brightness_is_255_no_overflow(void) {
    TEST_ASSERT_EQUAL_UINT8(255, status_led_core_scale_brightness(255, 255));
}

void test_scale_brightness_one_matches_exact_formula_for_nonzero_channel(void) {
    /* (uint32_t)200 * 1 / 255 == 0 -- the formula is exact fixed-point scaling,
     * not "never go fully dark," so this is the documented (and correct)
     * behavior at the very bottom of the brightness range. */
    TEST_ASSERT_EQUAL_UINT8((uint8_t)((200u * 1u) / 255u), status_led_core_scale_brightness(200, 1));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)((255u * 1u) / 255u), status_led_core_scale_brightness(255, 1));
}

void test_scale_brightness_is_monotonic_in_brightness_for_fixed_channel(void) {
    uint8_t channel = 200;
    uint8_t prev = status_led_core_scale_brightness(channel, 0);
    for (int b = 1; b <= 255; b++) {
        uint8_t cur = status_led_core_scale_brightness(channel, (uint8_t)b);
        TEST_ASSERT_TRUE(cur >= prev);
        prev = cur;
    }
}

void test_scale_brightness_zero_channel_is_always_zero(void) {
    TEST_ASSERT_EQUAL_UINT8(0, status_led_core_scale_brightness(0, 0));
    TEST_ASSERT_EQUAL_UINT8(0, status_led_core_scale_brightness(0, 128));
    TEST_ASSERT_EQUAL_UINT8(0, status_led_core_scale_brightness(0, 255));
}

void test_scale_brightness_zero_brightness_is_always_zero(void) {
    TEST_ASSERT_EQUAL_UINT8(0, status_led_core_scale_brightness(255, 0));
    TEST_ASSERT_EQUAL_UINT8(0, status_led_core_scale_brightness(1, 0));
}

/* ================= Solid-state stability: no accidental flicker ================= */

void test_frame_green_solid_is_identical_at_ticks_zero_one_and_hundred(void) {
    status_led_rgb_t f0 = status_led_core_frame(STATUS_LED_STATE_GREEN_SOLID, 0, 10, 255);
    status_led_rgb_t f1 = status_led_core_frame(STATUS_LED_STATE_GREEN_SOLID, 1, 10, 255);
    status_led_rgb_t f100 = status_led_core_frame(STATUS_LED_STATE_GREEN_SOLID, 100, 10, 255);

    TEST_ASSERT_EQUAL_UINT8(0, f0.r);
    TEST_ASSERT_EQUAL_UINT8(255, f0.g);
    TEST_ASSERT_EQUAL_UINT8(0, f0.b);

    TEST_ASSERT_EQUAL_UINT8(f0.r, f1.r);
    TEST_ASSERT_EQUAL_UINT8(f0.g, f1.g);
    TEST_ASSERT_EQUAL_UINT8(f0.b, f1.b);
    TEST_ASSERT_EQUAL_UINT8(f0.r, f100.r);
    TEST_ASSERT_EQUAL_UINT8(f0.g, f100.g);
    TEST_ASSERT_EQUAL_UINT8(f0.b, f100.b);
}

void test_frame_red_solid_is_identical_at_ticks_zero_one_and_hundred(void) {
    status_led_rgb_t f0 = status_led_core_frame(STATUS_LED_STATE_RED_SOLID, 0, 10, 255);
    status_led_rgb_t f1 = status_led_core_frame(STATUS_LED_STATE_RED_SOLID, 1, 10, 255);
    status_led_rgb_t f100 = status_led_core_frame(STATUS_LED_STATE_RED_SOLID, 100, 10, 255);

    TEST_ASSERT_EQUAL_UINT8(255, f0.r);
    TEST_ASSERT_EQUAL_UINT8(0, f0.g);
    TEST_ASSERT_EQUAL_UINT8(0, f0.b);

    TEST_ASSERT_EQUAL_UINT8(f0.r, f1.r);
    TEST_ASSERT_EQUAL_UINT8(f0.g, f1.g);
    TEST_ASSERT_EQUAL_UINT8(f0.b, f1.b);
    TEST_ASSERT_EQUAL_UINT8(f0.r, f100.r);
    TEST_ASSERT_EQUAL_UINT8(f0.g, f100.g);
    TEST_ASSERT_EQUAL_UINT8(f0.b, f100.b);
}

void test_frame_red_blink_alternates_lit_and_dark_with_blink_phase(void) {
    status_led_rgb_t lit = status_led_core_frame(STATUS_LED_STATE_RED_BLINK, 0, 10, 255);
    status_led_rgb_t dark = status_led_core_frame(STATUS_LED_STATE_RED_BLINK, 10, 10, 255);

    TEST_ASSERT_EQUAL_UINT8(255, lit.r);
    TEST_ASSERT_EQUAL_UINT8(0, lit.g);
    TEST_ASSERT_EQUAL_UINT8(0, lit.b);

    TEST_ASSERT_EQUAL_UINT8(0, dark.r);
    TEST_ASSERT_EQUAL_UINT8(0, dark.g);
    TEST_ASSERT_EQUAL_UINT8(0, dark.b);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_derive_state_wifi_unknown_http_down_is_red_solid);
    RUN_TEST(test_derive_state_wifi_up_http_down_is_red_solid);
    RUN_TEST(test_derive_state_wifi_down_http_down_is_red_solid);
    RUN_TEST(test_derive_state_wifi_up_http_up_is_green_solid);
    RUN_TEST(test_derive_state_wifi_unknown_http_unknown_is_red_blink);
    RUN_TEST(test_derive_state_wifi_unknown_http_up_is_red_blink);
    RUN_TEST(test_derive_state_wifi_up_http_unknown_is_red_blink);
    RUN_TEST(test_derive_state_wifi_down_http_unknown_is_red_blink);
    RUN_TEST(test_derive_state_wifi_down_http_up_is_red_blink);

    RUN_TEST(test_blink_is_lit_at_tick_zero);
    RUN_TEST(test_blink_is_dark_at_tick_equal_to_blink_ticks);
    RUN_TEST(test_blink_is_lit_again_at_tick_equal_to_two_times_blink_ticks);
    RUN_TEST(test_blink_with_blink_ticks_one_alternates_every_tick);
    RUN_TEST(test_blink_with_zero_blink_ticks_is_always_lit_and_does_not_crash);
    RUN_TEST(test_blink_wraps_correctly_near_uint32_max);

    RUN_TEST(test_scale_brightness_full_passes_channel_through_unchanged);
    RUN_TEST(test_scale_brightness_max_channel_max_brightness_is_255_no_overflow);
    RUN_TEST(test_scale_brightness_one_matches_exact_formula_for_nonzero_channel);
    RUN_TEST(test_scale_brightness_is_monotonic_in_brightness_for_fixed_channel);
    RUN_TEST(test_scale_brightness_zero_channel_is_always_zero);
    RUN_TEST(test_scale_brightness_zero_brightness_is_always_zero);

    RUN_TEST(test_frame_green_solid_is_identical_at_ticks_zero_one_and_hundred);
    RUN_TEST(test_frame_red_solid_is_identical_at_ticks_zero_one_and_hundred);
    RUN_TEST(test_frame_red_blink_alternates_lit_and_dark_with_blink_phase);

    return UNITY_END();
}
