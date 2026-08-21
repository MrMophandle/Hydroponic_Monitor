/**
 * status_led_task — starts the 100 ms tick task that drives the onboard
 * WS2812 status LED from the two reachability facts recorded in
 * `device_status` (see lib/device_status/include/device_status.h), via the
 * pure presentation policy in `lib/status_led_core`. Owns the LED's RMT
 * peripheral through `lib/status_led` (status_led_init()/status_led_show())
 * — no other module may call those directly.
 *
 * Device-only: not compiled or exercised under `[env:native]`. Mirrors the
 * shape of include/sampler.h's sampler_start(): a single start function
 * that app_main() calls once at boot, returning an esp_err_t the caller
 * must check rather than assume.
 */
#ifndef HYDROPONIC_MONITOR_STATUS_LED_TASK_H
#define HYDROPONIC_MONITOR_STATUS_LED_TASK_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Starts the status LED tick task.
 *
 * If CONFIG_HYDRO_STATUS_LED_ENABLE is disabled (Kconfig bool, default y),
 * this returns ESP_OK immediately without touching the RMT peripheral and
 * without creating any task (AC-VERIFY-8) — the documented escape hatch for
 * a board where the onboard LED GPIO turns out to be wrong or absent.
 *
 * Otherwise, initializes the WS2812 driver (status_led_init()) and creates
 * the tick task. On either step failing, logs the error and returns it
 * without creating a task — caller is app_main(), which mirrors the
 * non-fatal sampler_start()/http_api_start() failure handling already in
 * place there (the LED is a status indicator, not a safety-critical path;
 * a failure here must not abort boot).
 *
 * @return ESP_OK when disabled-by-config, or when the task was created;
 *         the underlying esp_err_t otherwise.
 */
esp_err_t status_led_start(void);

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_STATUS_LED_TASK_H */
