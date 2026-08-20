/**
 * test_level_switches — Unity suite for the pure level_switches debounce
 * state machine. Runs entirely on the host ([env:native]); no FreeRTOS, no
 * board, no gpio_* calls.
 *
 * Deliberately NOT tested here: reading the physical GPIOs themselves — that
 * is a thin device-only caller's job in a later phase (sensor_hub), not this
 * module's.
 */
#include <unity.h>

#include "level_switches.h"

static level_switches_t sw;

void setUp(void) {
    level_switches_init(&sw, false, false);
}

void tearDown(void) {}

/* ---- truth table: each combination maps to the correct state after N=3 agreeing samples ---- */

void test_both_floating_resolves_to_full(void) {
    level_switch_state_t result = LEVEL_SWITCH_STATE_UNKNOWN;
    for (int i = 0; i < LEVEL_SWITCHES_DEBOUNCE_N; i++) {
        result = level_switches_update(&sw, true, true);
    }
    TEST_ASSERT_EQUAL(LEVEL_SWITCH_STATE_FULL, result);
    TEST_ASSERT_EQUAL(LEVEL_SWITCH_STATE_FULL, level_switches_state(&sw));
}

void test_high_not_floating_low_floating_resolves_to_mid(void) {
    level_switch_state_t result = LEVEL_SWITCH_STATE_UNKNOWN;
    for (int i = 0; i < LEVEL_SWITCHES_DEBOUNCE_N; i++) {
        result = level_switches_update(&sw, false, true);
    }
    TEST_ASSERT_EQUAL(LEVEL_SWITCH_STATE_MID, result);
}

void test_both_not_floating_resolves_to_low(void) {
    level_switch_state_t result = LEVEL_SWITCH_STATE_UNKNOWN;
    for (int i = 0; i < LEVEL_SWITCHES_DEBOUNCE_N; i++) {
        result = level_switches_update(&sw, false, false);
    }
    TEST_ASSERT_EQUAL(LEVEL_SWITCH_STATE_LOW, result);
}

void test_high_floating_low_not_floating_resolves_to_fault(void) {
    level_switch_state_t result = LEVEL_SWITCH_STATE_UNKNOWN;
    for (int i = 0; i < LEVEL_SWITCHES_DEBOUNCE_N; i++) {
        result = level_switches_update(&sw, true, false);
    }
    TEST_ASSERT_EQUAL(LEVEL_SWITCH_STATE_FAULT, result);
}

/* ---- debounce timing ---- */

void test_state_change_rejected_before_n_consecutive_samples(void) {
    /* Only N-1 agreeing samples: must NOT commit FULL yet. */
    level_switch_state_t result = LEVEL_SWITCH_STATE_UNKNOWN;
    for (int i = 0; i < LEVEL_SWITCHES_DEBOUNCE_N - 1; i++) {
        result = level_switches_update(&sw, true, true);
    }
    TEST_ASSERT_EQUAL(LEVEL_SWITCH_STATE_UNKNOWN, result);
}

void test_flapping_sequence_never_commits_a_change(void) {
    /* Alternate between FULL-mapped and LOW-mapped raw inputs — never N
     * consecutive agreeing samples on either candidate, so the committed
     * state must remain UNKNOWN throughout. */
    level_switch_state_t result = LEVEL_SWITCH_STATE_UNKNOWN;
    for (int i = 0; i < 10; i++) {
        bool floating = (i % 2) == 0;
        result = level_switches_update(&sw, floating, floating);
    }
    TEST_ASSERT_EQUAL(LEVEL_SWITCH_STATE_UNKNOWN, result);
}

void test_sustained_change_commits_after_n_agreeing_samples_from_prior_state(void) {
    /* Establish a committed FULL state first. */
    for (int i = 0; i < LEVEL_SWITCHES_DEBOUNCE_N; i++) {
        level_switches_update(&sw, true, true);
    }
    TEST_ASSERT_EQUAL(LEVEL_SWITCH_STATE_FULL, level_switches_state(&sw));

    /* A single conflicting sample must not immediately flip the committed
     * state. */
    level_switch_state_t result = level_switches_update(&sw, false, false);
    TEST_ASSERT_EQUAL(LEVEL_SWITCH_STATE_FULL, result);

    /* N-1 more agreeing samples completes the sustained transition to LOW. */
    for (int i = 0; i < LEVEL_SWITCHES_DEBOUNCE_N - 1; i++) {
        result = level_switches_update(&sw, false, false);
    }
    TEST_ASSERT_EQUAL(LEVEL_SWITCH_STATE_LOW, result);
}

/* ---- FAULT is reported, never resolved to a neighboring band ---- */

void test_fault_not_resolved_to_neighboring_band_while_flapping_with_full(void) {
    /* Commit FULL first. */
    for (int i = 0; i < LEVEL_SWITCHES_DEBOUNCE_N; i++) {
        level_switches_update(&sw, true, true);
    }
    /* Alternate FAULT-mapped and FULL-mapped raw inputs: never N consecutive
     * agreeing FAULT samples, so committed state must stay FULL — it must
     * never silently drift to MID or LOW either. */
    level_switch_state_t result = LEVEL_SWITCH_STATE_UNKNOWN;
    for (int i = 0; i < 6; i++) {
        bool fault_sample = (i % 2) == 0;
        result = level_switches_update(&sw, true, fault_sample ? false : true);
    }
    TEST_ASSERT_EQUAL(LEVEL_SWITCH_STATE_FULL, result);
}

void test_sustained_fault_commits_and_is_reported_explicitly(void) {
    level_switch_state_t result = LEVEL_SWITCH_STATE_UNKNOWN;
    for (int i = 0; i < LEVEL_SWITCHES_DEBOUNCE_N; i++) {
        result = level_switches_update(&sw, true, false);
    }
    TEST_ASSERT_EQUAL(LEVEL_SWITCH_STATE_FAULT, result);
    TEST_ASSERT_NOT_EQUAL(LEVEL_SWITCH_STATE_MID, result);
    TEST_ASSERT_NOT_EQUAL(LEVEL_SWITCH_STATE_LOW, result);
    TEST_ASSERT_NOT_EQUAL(LEVEL_SWITCH_STATE_FULL, result);
}

/* ---- polarity inversion ---- */

void test_polarity_inversion_applied_correctly(void) {
    /* Both switches physically reversed: raw HIGH now means "not floating".
     * Init with invert_high=true, invert_low=true so raw (true, true) must
     * resolve to LOW (both logically not-floating), not FULL. */
    level_switches_t inverted_sw;
    level_switches_init(&inverted_sw, true, true);

    level_switch_state_t result = LEVEL_SWITCH_STATE_UNKNOWN;
    for (int i = 0; i < LEVEL_SWITCHES_DEBOUNCE_N; i++) {
        result = level_switches_update(&inverted_sw, true, true);
    }
    TEST_ASSERT_EQUAL(LEVEL_SWITCH_STATE_LOW, result);

    /* Raw (false, false) must now resolve to FULL under inversion. */
    level_switches_init(&inverted_sw, true, true);
    for (int i = 0; i < LEVEL_SWITCHES_DEBOUNCE_N; i++) {
        result = level_switches_update(&inverted_sw, false, false);
    }
    TEST_ASSERT_EQUAL(LEVEL_SWITCH_STATE_FULL, result);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_both_floating_resolves_to_full);
    RUN_TEST(test_high_not_floating_low_floating_resolves_to_mid);
    RUN_TEST(test_both_not_floating_resolves_to_low);
    RUN_TEST(test_high_floating_low_not_floating_resolves_to_fault);
    RUN_TEST(test_state_change_rejected_before_n_consecutive_samples);
    RUN_TEST(test_flapping_sequence_never_commits_a_change);
    RUN_TEST(test_sustained_change_commits_after_n_agreeing_samples_from_prior_state);
    RUN_TEST(test_fault_not_resolved_to_neighboring_band_while_flapping_with_full);
    RUN_TEST(test_sustained_fault_commits_and_is_reported_explicitly);
    RUN_TEST(test_polarity_inversion_applied_correctly);
    return UNITY_END();
}
