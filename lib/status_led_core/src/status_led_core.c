/**
 * status_led_core — see lib/status_led_core/include/status_led_core.h for
 * the module-level design rationale (pure onboard-status-LED policy core).
 */
#include "status_led_core.h"

status_led_state_t status_led_core_derive_state(status_facts_t facts) {
    if (facts.http == STATUS_FACT_DOWN) {
        return STATUS_LED_STATE_RED_SOLID;
    }
    if (facts.wifi == STATUS_FACT_UP && facts.http == STATUS_FACT_UP) {
        return STATUS_LED_STATE_GREEN_SOLID;
    }
    return STATUS_LED_STATE_RED_BLINK;
}

bool status_led_core_blink_is_lit(uint32_t tick, uint32_t blink_ticks) {
    if (blink_ticks == 0) {
        return true;
    }
    return ((tick / blink_ticks) % 2) == 0;
}

uint8_t status_led_core_scale_brightness(uint8_t channel, uint8_t brightness) {
    return (uint8_t)(((uint32_t)channel * (uint32_t)brightness) / 255u);
}

static status_led_rgb_t scale_rgb(status_led_rgb_t rgb, uint8_t brightness) {
    status_led_rgb_t out;
    out.r = status_led_core_scale_brightness(rgb.r, brightness);
    out.g = status_led_core_scale_brightness(rgb.g, brightness);
    out.b = status_led_core_scale_brightness(rgb.b, brightness);
    return out;
}

status_led_rgb_t status_led_core_frame(status_led_state_t state, uint32_t tick,
                                        uint32_t blink_ticks, uint8_t brightness) {
    static const status_led_rgb_t kGreen = {0, 255, 0};
    static const status_led_rgb_t kRed = {255, 0, 0};
    static const status_led_rgb_t kBlack = {0, 0, 0};

    switch (state) {
        case STATUS_LED_STATE_GREEN_SOLID:
            return scale_rgb(kGreen, brightness);
        case STATUS_LED_STATE_RED_SOLID:
            return scale_rgb(kRed, brightness);
        case STATUS_LED_STATE_RED_BLINK:
            return status_led_core_blink_is_lit(tick, blink_ticks) ? scale_rgb(kRed, brightness)
                                                                     : kBlack;
        default:
            return kBlack;
    }
}
