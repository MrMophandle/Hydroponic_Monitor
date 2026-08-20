/**
 * level_switches_stub — native-only, test-controllable substitute for
 * sensor_hub's water-level-read seam (sensor_hub_level_read()). See
 * sensor_hub_stubs.h.
 *
 * This does NOT replace lib/level_switches/src/level_switches.c (the pure
 * debounce state machine, already exercised directly by
 * test/test_level_switches/) — it stubs the separate, device-only GPIO
 * acquisition step (real body in src/sampler.c) that samples the physical
 * float switches and feeds them through that state machine once per sample
 * cycle. sensor_hub never calls level_switches_update() itself; it only
 * calls this seam and treats a non-OK return as a failed cycle for the
 * level sensor, symmetric with the light and temperature seams.
 */
#include "sensor_hub_stubs.h"

esp_err_t level_switches_stub_next_result = ESP_OK;
level_switch_state_t level_switches_stub_next_state = LEVEL_SWITCH_STATE_UNKNOWN;

void level_switches_stub_reset(void) {
    level_switches_stub_next_result = ESP_OK;
    level_switches_stub_next_state = LEVEL_SWITCH_STATE_UNKNOWN;
}

esp_err_t sensor_hub_level_read(level_switch_state_t *level_out) {
    if (level_switches_stub_next_result == ESP_OK) {
        *level_out = level_switches_stub_next_state;
    }
    return level_switches_stub_next_result;
}
