/**
 * sampler — the 30-second (Kconfig `CONFIG_HYDRO_SAMPLE_INTERVAL_SEC`)
 * sensor-sampling FreeRTOS task, and the single owner of every sensor
 * peripheral in the firmware.
 *
 * Device-only: not compiled or exercised under `[env:native]`. Owns the
 * actual driver handles (BH1750, DS18B20 probe, level-switch GPIO + debounce
 * state) and supplies the real bodies of sensor_hub's three read seams
 * (`sensor_hub_light_read()`, `sensor_hub_temp_read()`,
 * `sensor_hub_level_read()` — see lib/sensor_hub/include/sensor_hub.h).
 * Each cycle it runs `sensor_hub_run_cycle()` bracketed by a task-watchdog
 * subscription scoped to just the read window (never the sleep — see
 * sampler.c for why), then pushes the resulting sample into `reading_store`.
 *
 * Peripheral ownership (systemPatterns.md § Guiding Principles → One Owner
 * Per Peripheral): this module creates the I2C master bus, the 1-Wire RMT
 * bus, and the level-switch GPIO configuration EXACTLY ONCE, in
 * `sampler_sensors_init()`. Nothing else in the firmware may call
 * `i2c_new_master_bus()`, `onewire_new_bus_rmt()` or `gpio_config()` for
 * these pins — ESP-IDF's acquire calls are not idempotent (a second
 * `i2c_new_master_bus()` on the same port returns `ESP_ERR_INVALID_STATE`),
 * so a second creator does not get its own bus, it gets a hard failure.
 * Callers that need a sensor reading — including `app_main()`'s boot read —
 * go through the `sensor_hub_*_read()` seams rather than standing up their
 * own drivers.
 */
#ifndef HYDROPONIC_MONITOR_SAMPLER_H
#define HYDROPONIC_MONITOR_SAMPLER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Creates the shared sensor peripherals and initializes all three drivers:
 * the I2C master bus + BH1750, the 1-Wire RMT bus + DS18B20 probe, and the
 * level-switch GPIO configuration + debounce state.
 *
 * Must be called EXACTLY ONCE, before `sampler_start()` and before any
 * `sensor_hub_*_read()` call. Calling it twice will fail on the second call
 * (see the ownership note above), and is a bug, not a recoverable condition.
 *
 * Each of the three drivers is attempted independently: one failing does not
 * skip the others, and a failed driver leaves its read seam returning
 * `ESP_FAIL` every cycle, which feeds sensor_hub's normal
 * consecutive-failure/offline escalation rather than crashing or
 * dereferencing an uninitialized handle.
 *
 * @return ESP_OK when all three drivers initialized. ESP_FAIL when one or
 *         more failed — the caller should log and continue (sampling still
 *         runs with whichever sensors came up), not abort.
 */
esp_err_t sampler_sensors_init(void);

/**
 * Starts the sampler task.
 *
 * Must be called after `sampler_sensors_init()` (drivers must exist) and
 * after `reading_store_init()` — the first sample cycle can run essentially
 * immediately, and pushing to an un-initialized store would silently drop it
 * (see reading_store.c). Caller is `app_main()`.
 *
 * @return ESP_OK when the task was created; ESP_ERR_NO_MEM when
 *         `xTaskCreatePinnedToCore()` failed (typically heap exhaustion).
 *         A non-OK return means NOTHING will ever be sampled, so the caller
 *         must surface it rather than assume success.
 */
esp_err_t sampler_start(void);

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_SAMPLER_H */
