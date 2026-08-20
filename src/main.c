/**
 * app_main — boot wiring: initialize peripherals once, take one reading of
 * each sensor for the bench, then hand off to the continuous sampler and the
 * Wi-Fi layer and return.
 *
 * `app_main()` creates nothing itself. Every sensor peripheral (the I2C
 * master bus, the 1-Wire RMT bus, the level-switch GPIO configuration) is
 * owned by `sampler.c` and stood up exactly once by
 * `sampler_sensors_init()`; the boot read below then goes through the same
 * `sensor_hub_*_read()` seams the sampler task uses. See
 * include/sampler.h for the ownership contract and
 * systemPatterns.md § Guiding Principles → One Owner Per Peripheral.
 *
 * This replaced an earlier arrangement where the boot read stood up its own
 * I2C and 1-Wire buses in parallel with the sampler's. That was not a
 * harmless duplication: ESP-IDF's acquire calls are not idempotent, so the
 * sampler's `i2c_new_master_bus()` got `ESP_ERR_INVALID_STATE` and the
 * BH1750 stayed un-ready for the entire life of every boot (the lux series
 * was all invalid bits), while the duplicate 1-Wire acquire quietly orphaned
 * an RMT TX+RX channel pair out of the ESP32-S3's four.
 *
 * Pins and switch polarity come from Kconfig (menu "Hydroponic Monitor"),
 * never from literals — float-switch polarity in particular cannot be
 * assumed and is a bench-determined value.
 */
#include <inttypes.h>
#include <stdbool.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include "device_status.h"
#include "http_api.h"
#include "level_switches.h"
#include "reading_store.h"
#include "sampler.h"
#include "sensor_hub.h"
#include "time_sync.h"
#include "wifi_conn.h"

static const char *TAG = "main";

static const char *level_state_name(level_switch_state_t state) {
    switch (state) {
        case LEVEL_SWITCH_STATE_FULL:
            return "FULL";
        case LEVEL_SWITCH_STATE_MID:
            return "MID";
        case LEVEL_SWITCH_STATE_LOW:
            return "LOW";
        case LEVEL_SWITCH_STATE_FAULT:
            return "FAULT";
        case LEVEL_SWITCH_STATE_UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Hydroponic Monitor boot");

    /* DRAM baseline for the Phase 5 re-check, an outstanding item carried over
     * from Phase 1 (which could not log it: app_main() was still empty). */
    ESP_LOGI(TAG, "free heap at boot: %" PRIu32 " bytes", esp_get_free_heap_size());

    /* Single peripheral-acquisition point for the whole firmware. A degraded
     * return is not fatal: each driver is attempted independently, and any
     * that failed leaves its read seam returning ESP_FAIL, which feeds
     * sensor_hub's normal offline escalation. Sampling still runs on
     * whichever sensors came up. */
    bool sensor_fault = (sampler_sensors_init() != ESP_OK);

    /* One-shot boot read, through the same seams the sampler uses. Failed
     * reads leave their output untouched and are never reported as 0.0. */
    float lux = 0.0f;
    if (sensor_hub_light_read(&lux) == ESP_OK) {
        ESP_LOGI(TAG, "ambient light: %.1f lux", lux);
    } else {
        ESP_LOGW(TAG, "ambient light: read failed");
        sensor_fault = true;
    }

    float temp_c = 0.0f;
    if (sensor_hub_temp_read(&temp_c) == ESP_OK) {
        ESP_LOGI(TAG, "water temperature: %.2f C", temp_c);
    } else {
        ESP_LOGW(TAG, "water temperature: read failed");
        sensor_fault = true;
    }

    /* sensor_hub_level_read() takes LEVEL_SWITCHES_DEBOUNCE_N spaced samples
     * internally, so the state machine can commit out of UNKNOWN — a single
     * raw sample would only ever report UNKNOWN. */
    level_switch_state_t level_state = LEVEL_SWITCH_STATE_UNKNOWN;
    if (sensor_hub_level_read(&level_state) == ESP_OK) {
        ESP_LOGI(TAG, "water level: %s (high GPIO %d, low GPIO %d)", level_state_name(level_state),
                 CONFIG_HYDRO_LEVEL_HIGH_GPIO, CONFIG_HYDRO_LEVEL_LOW_GPIO);
    } else {
        ESP_LOGW(TAG, "water level: read failed");
        sensor_fault = true;
    }

    /* LEVEL_FAULT is the more specific and more urgent condition, so it wins
     * when both are true — a stuck float is a physical wiring problem. */
    if (level_state == LEVEL_SWITCH_STATE_FAULT) {
        status_set(DEVICE_STATUS_LEVEL_FAULT);
    } else if (sensor_fault) {
        status_set(DEVICE_STATUS_SENSOR_FAULT);
    } else {
        status_set(DEVICE_STATUS_OK);
    }

    ESP_LOGI(TAG, "boot read complete");

    /* Phase 3: start the continuous sampler + the ring store it feeds.
     * reading_store_init() must run before sampler_start(), since the first
     * sample cycle can complete almost immediately and a push before init
     * would be silently dropped (see reading_store.c). app_main() stays
     * thin — all task/driver wiring lives in sampler.c. */
    reading_store_init();
    esp_err_t sampler_err = sampler_start();
    if (sampler_err == ESP_OK) {
        ESP_LOGI(TAG, "sampler started (interval: %d s)", CONFIG_HYDRO_SAMPLE_INTERVAL_SEC);
    } else {
        /* Do not claim the sampler started when it did not — with no task
         * there is no history at all, which is a louder failure than any
         * single sensor going offline. */
        ESP_LOGE(TAG, "sampler FAILED to start (%s) — no readings will be recorded",
                 esp_err_to_name(sampler_err));
        status_set(DEVICE_STATUS_SENSOR_FAULT);
    }

    /* Phase 4: station-mode Wi-Fi + mDNS. Deliberately independent of the
     * sampler above — connectivity is the delivery channel, not the
     * measurement (AC-ERROR-3), so it starts after sampling regardless of
     * whether it ever succeeds. wifi_conn_start() wires event handlers and
     * returns immediately; the actual connect/reconnect sequence runs
     * asynchronously. */
    wifi_conn_start();
    ESP_LOGI(TAG, "wifi connectivity starting");

    /* Phase 5: SNTP wall-clock time (needs the Wi-Fi just started above —
     * see include/time_sync.h) and the HTTP API (/, /api/now,
     * /api/history). Neither owns a sensor peripheral; http_api reads
     * through reading_store, same as any other consumer would. */
    esp_err_t time_sync_err = time_sync_start();
    if (time_sync_err == ESP_OK) {
        ESP_LOGI(TAG, "SNTP time sync starting");
    } else {
        /* Non-fatal: readings keep recording with their time_valid bit
         * clear until this is fixed and a sync eventually completes. */
        ESP_LOGW(TAG, "SNTP time sync FAILED to start (%s) — timestamps stay unsynced",
                 esp_err_to_name(time_sync_err));
    }

    esp_err_t http_err = http_api_start();
    if (http_err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP API started");
    } else {
        /* Unlike a single sensor going offline, this means the dashboard is
         * entirely unreachable — mirror how sampler_start() failure is
         * surfaced above. No dedicated "API down" status exists yet;
         * WIFI_DOWN is the closest existing seam for "the delivery channel
         * to the user is down" and is reused here rather than inventing a
         * new device_status_t value in this phase. */
        ESP_LOGE(TAG, "HTTP API FAILED to start (%s) — dashboard is unreachable",
                 esp_err_to_name(http_err));
        status_set(DEVICE_STATUS_WIFI_DOWN);
    }
}
