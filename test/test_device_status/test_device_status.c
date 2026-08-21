/**
 * test_device_status — Unity suite for the device_status reporting seam
 * (Phase 2 of onboard-status-led). Runs entirely on the host ([env:native]);
 * no FreeRTOS, no Wi-Fi radio, no HTTP server.
 *
 * `status_report_wifi()` / `status_report_http()` / `status_snapshot()` wrap
 * module-level static state that models a real singleton (there is exactly
 * one Wi-Fi radio and one HTTP server per device), so — per this project's
 * convention for such modules — each test's setUp() resets both facts back
 * to STATUS_FACT_UNKNOWN through the real public API rather than a
 * test-only backdoor (STATUS_FACT_UNKNOWN is already a legal input to the
 * production API).
 *
 * Deliberately NOT tested here: the ESP_LOGI log-on-change line itself (it
 * is a no-op macro under [env:native] via esp_shim.h — see
 * lib/sensor_hub/include/sensor_hub.h for the precedent — and there is no
 * hook to observe it on host); the real Wi-Fi/HTTP call sites in
 * src/wifi_conn.c and src/main.c (device-only wiring, verified on the bench
 * per the Test Strategy, not host-tested).
 */
#include <unity.h>

#include "device_status.h"
#include "status_led_core.h"

void setUp(void) {
    status_report_wifi(STATUS_FACT_UNKNOWN);
    status_report_http(STATUS_FACT_UNKNOWN);
}

void tearDown(void) {}

/* ================= status_snapshot() reflects the latest reported facts ================= */

void test_snapshot_reflects_latest_wifi_fact(void) {
    status_report_wifi(STATUS_FACT_UP);
    status_facts_t snap = status_snapshot();
    TEST_ASSERT_EQUAL(STATUS_FACT_UP, snap.wifi);
}

void test_snapshot_reflects_latest_http_fact(void) {
    status_report_http(STATUS_FACT_DOWN);
    status_facts_t snap = status_snapshot();
    TEST_ASSERT_EQUAL(STATUS_FACT_DOWN, snap.http);
}

/* ================= Independence: reporting one fact must not disturb the other ================= */
/* This is the exact bug class this task exists to prevent — src/main.c's old
 * comment "no dedicated 'API down' status exists yet" was a symptom of this
 * kind of conflation in the OLD single-valued status_set() model. */

void test_reporting_wifi_does_not_disturb_current_http_fact(void) {
    status_report_http(STATUS_FACT_UP);
    status_report_wifi(STATUS_FACT_DOWN);
    status_facts_t snap = status_snapshot();
    TEST_ASSERT_EQUAL(STATUS_FACT_DOWN, snap.wifi);
    TEST_ASSERT_EQUAL(STATUS_FACT_UP, snap.http);
}

void test_reporting_http_does_not_disturb_current_wifi_fact(void) {
    status_report_wifi(STATUS_FACT_UP);
    status_report_http(STATUS_FACT_DOWN);
    status_facts_t snap = status_snapshot();
    TEST_ASSERT_EQUAL(STATUS_FACT_UP, snap.wifi);
    TEST_ASSERT_EQUAL(STATUS_FACT_DOWN, snap.http);
}

/* ================= Idempotency ================= */

void test_reporting_same_fact_twice_in_a_row_is_idempotent(void) {
    status_report_wifi(STATUS_FACT_UP);
    status_report_wifi(STATUS_FACT_UP);
    status_facts_t snap = status_snapshot();
    TEST_ASSERT_EQUAL(STATUS_FACT_UP, snap.wifi);

    status_report_http(STATUS_FACT_DOWN);
    status_report_http(STATUS_FACT_DOWN);
    snap = status_snapshot();
    TEST_ASSERT_EQUAL(STATUS_FACT_DOWN, snap.http);
}

/* ================= End-to-end wiring through the real reporting API ================= */

void test_both_up_derives_green_solid(void) {
    status_report_wifi(STATUS_FACT_UP);
    status_report_http(STATUS_FACT_UP);
    TEST_ASSERT_EQUAL(STATUS_LED_STATE_GREEN_SOLID, status_led_core_derive_state(status_snapshot()));
}

/* http == DOWN outranks everything, even a still-UP wifi fact. */

void test_http_down_outranks_wifi_up(void) {
    status_report_wifi(STATUS_FACT_UP);
    status_report_http(STATUS_FACT_UP);
    status_report_http(STATUS_FACT_DOWN);
    TEST_ASSERT_EQUAL(STATUS_LED_STATE_RED_SOLID, status_led_core_derive_state(status_snapshot()));
}

/* Boot window: both facts freshly UNKNOWN (as setUp() leaves them) must
 * present as RED_BLINK, never RED_SOLID — the exact "http initializes to
 * down and falsely claims permanently-broken for the first second of every
 * boot" bug the Phase 1 tri-state design exists to prevent, now checked
 * through the real reporting/snapshot seam. */

void test_boot_window_both_unknown_derives_red_blink_not_red_solid(void) {
    TEST_ASSERT_EQUAL(STATUS_LED_STATE_RED_BLINK, status_led_core_derive_state(status_snapshot()));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_snapshot_reflects_latest_wifi_fact);
    RUN_TEST(test_snapshot_reflects_latest_http_fact);
    RUN_TEST(test_reporting_wifi_does_not_disturb_current_http_fact);
    RUN_TEST(test_reporting_http_does_not_disturb_current_wifi_fact);
    RUN_TEST(test_reporting_same_fact_twice_in_a_row_is_idempotent);
    RUN_TEST(test_both_up_derives_green_solid);
    RUN_TEST(test_http_down_outranks_wifi_up);
    RUN_TEST(test_boot_window_both_unknown_derives_red_blink_not_red_solid);

    return UNITY_END();
}
