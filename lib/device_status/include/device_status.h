/**
 * device_status — device-wide status seam.
 *
 * v1 is log-only: status_set() emits a structured ESP_LOGI/W/E line at a
 * severity appropriate to the state. There is no LED (or other hardware)
 * consumer yet — the Hosyond ESP32-S3-WROOM-1's onboard status LED
 * availability/GPIO is unconfirmed (per the task's open questions), so the
 * hardware consumer is deferred until that pin is confirmed on the bench.
 * This module exists now so later phases (sensor_hub, wifi_conn, http_api)
 * have a single seam to call into rather than scattering ad-hoc logging.
 *
 * Phase 2 of the onboard-status-led task (see
 * memory-bank/tasks/onboard-status-led.md) adds a second, independent seam
 * alongside the original coarse status_set(): status_report_wifi() /
 * status_report_http() record the two tri-state reachability facts
 * (lib/status_led_core's status_fact_t) that the LED presentation policy
 * derives its state from, and status_snapshot() lets a reader (the Phase 3
 * LED tick task) pull the current combined facts. This creates a
 * device_status -> status_led_core dependency (device wrapper depending on
 * the pure policy core, the correct direction per "One Owner Per
 * Peripheral"). status_set()/device_status_t are untouched by this phase.
 */
#ifndef HYDROPONIC_MONITOR_DEVICE_STATUS_H
#define HYDROPONIC_MONITOR_DEVICE_STATUS_H

#include "status_led_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Device-wide status states. Not a per-sensor validity flag (that lives in
 * sensor_reading_t.valid, from reading_store_core) — this is the coarse,
 * user-relevant device condition that a future status LED / dashboard badge
 * would reflect.
 */
typedef enum {
    DEVICE_STATUS_OK = 0,     /* nominal: all sensors reading, Wi-Fi connected */
    DEVICE_STATUS_SENSOR_FAULT, /* a sensor is offline (consecutive read failures) */
    DEVICE_STATUS_LEVEL_FAULT,  /* the water-level switches report FAULT */
    DEVICE_STATUS_WIFI_DOWN,    /* Wi-Fi is disconnected; sampling continues regardless */
} device_status_t;

/**
 * Records a device status transition. Logs at a severity matching the
 * state: OK -> ESP_LOGI, WIFI_DOWN -> ESP_LOGW, {SENSOR,LEVEL}_FAULT ->
 * ESP_LOGE. Never blocks, never fails.
 */
void status_set(device_status_t status);

/**
 * Records the current Wi-Fi association fact. Single writer in production:
 * the WIFI_EVENT_STA_DISCONNECTED and IP_EVENT_STA_GOT_IP handlers in
 * src/wifi_conn.c. Re-derives the combined LED state via
 * status_led_core_derive_state() and logs at ESP_LOGI only when that
 * derived state differs from the last logged state (log-on-change,
 * mirroring the transmit-on-change convention used by status_led_show() in
 * Phase 3 of this same task) — so serial output shows LED-relevant
 * transitions, not one line per Wi-Fi event. Never blocks, never fails.
 *
 * Note: status_report_wifi() and status_report_http() share one
 * s_last_logged_state sentinel, so back-to-back concurrent reports from
 * each side can race on which one observes/logs a given transition (a
 * duplicate or missed log line) -- never a wrong *derived* LED state,
 * since status_snapshot() always re-derives from two fresh atomic loads.
 */
void status_report_wifi(status_fact_t fact);

/**
 * Records the current HTTP-server-up fact. Single writer in production: the
 * http_api_start() call site in src/main.c. Same log-on-change contract as
 * status_report_wifi() above. Never blocks, never fails.
 */
void status_report_http(status_fact_t fact);

/**
 * Returns the current snapshot of both reachability facts. Implemented as
 * two independent atomic loads with no lock: each fact has exactly one
 * writer in production and there is no read-modify-write across the pair,
 * so a plain atomic_int per fact is sufficient — no mutex needed (see
 * memory-bank/creative/onboard-status-led-design.md Decision 5). A reader
 * may observe the two facts from slightly different moments in time under
 * concurrent writes, which is acceptable: the next write's log-on-change
 * check will correct the presented LED state on its next report.
 */
status_facts_t status_snapshot(void);

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_DEVICE_STATUS_H */
