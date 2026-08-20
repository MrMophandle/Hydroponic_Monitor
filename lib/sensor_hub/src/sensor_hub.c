/**
 * sensor_hub — see lib/sensor_hub/include/sensor_hub.h for the module-level
 * design rationale (link-time-substituted read seams, failure-independence,
 * five-consecutive-failure offline escalation).
 */
#include "sensor_hub.h"

#include <string.h>

static const char *TAG = "sensor_hub";

/**
 * Applies one sensor's read result to its failure/offline tracking state and
 * returns whether the reading is valid for this cycle.
 *
 * On success: the counter resets to 0 and any prior offline flag is cleared
 * — a single good read is enough to bring a sensor back online.
 * On failure: the counter increments (saturating, never wrapping past
 * UINT8_MAX) and offline is set once it reaches SENSOR_HUB_OFFLINE_THRESHOLD.
 * Nothing here aborts the caller's cycle — the other sensors are unaffected.
 */
static bool track_result(uint8_t *consecutive_failures, bool *offline, esp_err_t result) {
    if (result == ESP_OK) {
        *consecutive_failures = 0;
        *offline = false;
        return true;
    }

    if (*consecutive_failures < UINT8_MAX) {
        (*consecutive_failures)++;
    }
    if (*consecutive_failures >= SENSOR_HUB_OFFLINE_THRESHOLD) {
        *offline = true;
    }
    return false;
}

void sensor_hub_init(sensor_hub_t *hub) {
    memset(hub, 0, sizeof(*hub));
}

sensor_hub_reading_t sensor_hub_run_cycle(sensor_hub_t *hub) {
    sensor_hub_reading_t reading;
    memset(&reading, 0, sizeof(reading));
    reading.level = LEVEL_SWITCH_STATE_UNKNOWN;

    /* Each sensor is read and tracked independently — a failure on one never
     * skips or aborts the others (per the design doc's independent-failure
     * requirement). One retry is assumed to already have happened inside the
     * driver seam itself; sensor_hub treats a single non-OK return here as
     * this cycle's result for that sensor. */
    float lux = 0.0f;
    esp_err_t light_result = sensor_hub_light_read(&lux);
    reading.light_valid = track_result(&hub->consecutive_failures[SENSOR_HUB_SENSOR_LIGHT],
                                        &hub->offline[SENSOR_HUB_SENSOR_LIGHT], light_result);
    if (reading.light_valid) {
        reading.lux = lux;
    } else {
        ESP_LOGW(TAG, "light read failed (consecutive failures: %u)",
                 hub->consecutive_failures[SENSOR_HUB_SENSOR_LIGHT]);
    }

    float temp_c = 0.0f;
    esp_err_t temp_result = sensor_hub_temp_read(&temp_c);
    reading.temp_valid = track_result(&hub->consecutive_failures[SENSOR_HUB_SENSOR_TEMP],
                                       &hub->offline[SENSOR_HUB_SENSOR_TEMP], temp_result);
    if (reading.temp_valid) {
        reading.temp_c = temp_c;
    } else {
        ESP_LOGW(TAG, "temperature read failed (consecutive failures: %u)",
                 hub->consecutive_failures[SENSOR_HUB_SENSOR_TEMP]);
    }

    level_switch_state_t level = LEVEL_SWITCH_STATE_UNKNOWN;
    esp_err_t level_result = sensor_hub_level_read(&level);
    reading.level_valid = track_result(&hub->consecutive_failures[SENSOR_HUB_SENSOR_LEVEL],
                                        &hub->offline[SENSOR_HUB_SENSOR_LEVEL], level_result);
    if (reading.level_valid) {
        reading.level = level;
    } else {
        ESP_LOGW(TAG, "level read failed (consecutive failures: %u)",
                 hub->consecutive_failures[SENSOR_HUB_SENSOR_LEVEL]);
    }

    return reading;
}

bool sensor_hub_is_offline(const sensor_hub_t *hub, sensor_hub_sensor_id_t sensor) {
    return hub->offline[sensor];
}

uint8_t sensor_hub_consecutive_failures(const sensor_hub_t *hub, sensor_hub_sensor_id_t sensor) {
    return hub->consecutive_failures[sensor];
}
