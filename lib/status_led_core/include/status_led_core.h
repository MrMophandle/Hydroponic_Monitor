/**
 * status_led_core — pure policy core for the onboard status LED
 * (WS2812, GPIO 48 by Kconfig default; see the onboard-status-led task).
 *
 * This module owns ONLY the presentation policy: given the two independent
 * reachability facts (Wi-Fi association, HTTP server up), which of the three
 * presentation states applies; whether a blinking state is lit or dark at a
 * given tick; and how to scale an RGB channel by a brightness factor. It
 * takes NO ESP-IDF/FreeRTOS/RMT dependency (no rmt_*, no vTaskDelay, no
 * esp_wifi/esp_event calls), so it compiles and runs under the host-only
 * [env:native] PlatformIO environment, matching the wifi_backoff /
 * reading_store_core / level_switches split-module pattern from earlier
 * phases. The device-only half — actually driving the WS2812 over RMT and
 * running the tick task — lives in `lib/status_led/` and
 * `src/status_led_task.c` (Phase 2/3), which own no policy of their own and
 * delegate all of it here.
 *
 * Precedence (per the task's design decisions): `http == STATUS_FACT_DOWN`
 * outranks everything else, because it is permanent and unrecoverable without
 * a reflash. `UNKNOWN` is a genuine third state (not a boolean default) so
 * the boot window before either fact is known does not falsely present as
 * "permanently broken."
 */
#ifndef HYDROPONIC_MONITOR_STATUS_LED_CORE_H
#define HYDROPONIC_MONITOR_STATUS_LED_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Tri-state reachability fact: unknown (not yet observed), up, or down. */
typedef enum {
    STATUS_FACT_UNKNOWN,
    STATUS_FACT_UP,
    STATUS_FACT_DOWN,
} status_fact_t;

/** The two independent facts the LED policy derives its state from. */
typedef struct {
    status_fact_t wifi;
    status_fact_t http;
} status_facts_t;

/** The three LED presentation states. */
typedef enum {
    STATUS_LED_STATE_GREEN_SOLID,
    STATUS_LED_STATE_RED_BLINK,
    STATUS_LED_STATE_RED_SOLID,
} status_led_state_t;

/** A brightness-scaled RGB frame ready to hand to the WS2812 driver. */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} status_led_rgb_t;

/**
 * Derives the presentation state from the current reachability facts.
 * Total over all 9 (wifi, http) combinations:
 *   - http == STATUS_FACT_DOWN                    -> RED_SOLID (outranks all)
 *   - wifi == STATUS_FACT_UP && http == STATUS_FACT_UP -> GREEN_SOLID
 *   - otherwise                                    -> RED_BLINK
 */
status_led_state_t status_led_core_derive_state(status_facts_t facts);

/**
 * Returns whether a blinking LED is lit at the given tick, for a blink
 * half-period of blink_ticks. Lit at tick 0, dark at tick == blink_ticks, lit
 * again at tick == 2*blink_ticks, and so on. blink_ticks == 0 is a guarded
 * edge case (no division by zero): treated as always-lit.
 */
bool status_led_core_blink_is_lit(uint32_t tick, uint32_t blink_ticks);

/**
 * Scales a single 0-255 RGB channel value by a 0-255 brightness factor
 * (255 == full brightness, passes the channel through unchanged).
 */
uint8_t status_led_core_scale_brightness(uint8_t channel, uint8_t brightness);

/**
 * Produces the brightness-scaled RGB frame for the given state at the given
 * tick. GREEN_SOLID and RED_SOLID are identical at every tick (no flicker);
 * RED_BLINK alternates between the scaled red frame and black according to
 * status_led_core_blink_is_lit().
 */
status_led_rgb_t status_led_core_frame(status_led_state_t state, uint32_t tick,
                                        uint32_t blink_ticks, uint8_t brightness);

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_STATUS_LED_CORE_H */
