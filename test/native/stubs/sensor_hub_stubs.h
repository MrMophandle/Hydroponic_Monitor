/**
 * sensor_hub_stubs.h — test-controllable link-time substitutes for
 * sensor_hub's three per-sensor read seams (sensor_hub_light_read(),
 * sensor_hub_temp_read(), sensor_hub_level_read()), used only by
 * [env:native] to exercise sensor_hub's failure-handling logic
 * deterministically, without any real I2C/1-Wire/GPIO hardware.
 *
 * Convention (established here for Phase 3, no prior stub existed): each
 * stubbed sensor exposes an `extern <sensor>_stub_next_result` (the
 * esp_err_t the next call returns) and an `extern <sensor>_stub_next_<value>`
 * (the value written out on ESP_OK), plus a `<sensor>_stub_reset()` that
 * restores both to a default success state. Tests call the `_reset()`
 * functions from `setUp()` so every test starts from a known-good baseline
 * and only overrides what it needs to force a failure.
 */
#ifndef HYDROPONIC_MONITOR_SENSOR_HUB_STUBS_H
#define HYDROPONIC_MONITOR_SENSOR_HUB_STUBS_H

#include "esp_shim.h"
#include "level_switches.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- bh1750 (light) stub control ---- */
extern esp_err_t bh1750_stub_next_result;
extern float bh1750_stub_next_lux;
void bh1750_stub_reset(void);

/* ---- ds18b20_probe (temperature) stub control ---- */
extern esp_err_t ds18b20_probe_stub_next_result;
extern float ds18b20_probe_stub_next_temp_c;
void ds18b20_probe_stub_reset(void);

/* ---- level_switches (water level) stub control ---- */
extern esp_err_t level_switches_stub_next_result;
extern level_switch_state_t level_switches_stub_next_state;
void level_switches_stub_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_SENSOR_HUB_STUBS_H */
