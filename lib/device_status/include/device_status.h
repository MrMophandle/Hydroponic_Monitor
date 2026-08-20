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
 */
#ifndef HYDROPONIC_MONITOR_DEVICE_STATUS_H
#define HYDROPONIC_MONITOR_DEVICE_STATUS_H

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

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_DEVICE_STATUS_H */
