/**
 * sampler — the 30-second (Kconfig `CONFIG_HYDRO_SAMPLE_INTERVAL_SEC`)
 * sensor-sampling FreeRTOS task.
 *
 * Device-only: not compiled or exercised under `[env:native]`. Owns the
 * actual driver handles (BH1750, DS18B20 probe, level-switch GPIO + debounce
 * state) and supplies the real bodies of sensor_hub's three read seams
 * (`sensor_hub_light_read()`, `sensor_hub_temp_read()`,
 * `sensor_hub_level_read()` — see lib/sensor_hub/include/sensor_hub.h).
 * Each cycle it runs `sensor_hub_run_cycle()` bracketed by a task-watchdog
 * subscription scoped to just the read window (never the sleep — see
 * sampler.c for why), then pushes the resulting sample into `reading_store`.
 */
#ifndef HYDROPONIC_MONITOR_SAMPLER_H
#define HYDROPONIC_MONITOR_SAMPLER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initializes the sensor drivers (I2C bus + BH1750, 1-Wire bus + DS18B20
 * probe, level-switch GPIO configuration) and starts the sampler task.
 *
 * Must be called after `reading_store_init()` — the first sample cycle can
 * run essentially immediately, and pushing to an un-initialized store would
 * silently drop it (see reading_store.c). Caller is `app_main()`.
 */
void sampler_start(void);

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_SAMPLER_H */
