/**
 * status_led — device-only WS2812 (onboard RGB) driver.
 *
 * This module owns exactly one RMT TX channel and knows nothing about the
 * LED *policy* (which state to show, when to blink) — that is entirely
 * `lib/status_led_core`'s job (see status_led_core.h). This module's only
 * responsibility is turning an already-decided `status_led_rgb_t` frame
 * into WS2812 wire bits over RMT. Device-only: not compiled or exercised
 * under `[env:native]` (no RMT peripheral on the host).
 *
 * GPIO is a Kconfig choice (`CONFIG_HYDRO_STATUS_LED_GPIO_48/_47/_38`, see
 * src/Kconfig.projbuild), resolved to a literal `gpio_num_t` once, inside
 * status_led.c — never exposed here, per AC-VERIFY-6 (a Kconfig `choice`,
 * not an `int`+`range`, because the "correct" GPIO for this board's onboard
 * LED is genuinely disputed between vendor Q&A and third-party docs; see the
 * Kconfig help text).
 */
#ifndef HYDROPONIC_MONITOR_STATUS_LED_H
#define HYDROPONIC_MONITOR_STATUS_LED_H

#include "esp_err.h"
#include "status_led_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Creates the RMT TX channel and installs the WS2812 encoder. Must be
 * called exactly once, before the first status_led_show() call — mirrors
 * the one-shot peripheral-acquisition contract used elsewhere in this
 * firmware (see include/sampler.h's ownership note). This is the ONLY
 * caller of rmt_new_tx_channel() for the onboard status LED (AC-VERIFY-7).
 *
 * @return ESP_OK on success; the underlying RMT/encoder error otherwise.
 */
esp_err_t status_led_init(void);

/**
 * Transmits one WS2812 frame (blocks until the transmission completes).
 * status_led_init() must have already succeeded.
 *
 * @return ESP_OK on success; the underlying RMT error otherwise.
 */
esp_err_t status_led_show(status_led_rgb_t frame);

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_STATUS_LED_H */
