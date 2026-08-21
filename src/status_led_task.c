/**
 * status_led_task — see include/status_led_task.h for the module-level
 * design rationale. Device-only: sole owner of the onboard WS2812's RMT
 * peripheral (via lib/status_led) and the only caller of
 * status_led_init()/status_led_show().
 */
#include "status_led_task.h"

#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "device_status.h"
#include "status_led.h"
#include "status_led_core.h"

static const char *TAG = "status_led_task";

/* Task sizing. Priority 2 sits one above the ESP-IDF main task's default
 * priority (1), so the tick task reliably preempts app_main() during the
 * boot window instead of waiting behind it — load-bearing for AC-ASYNC-1,
 * which starts this task before the blocking boot sensor read specifically
 * so the LED can illuminate during that read. 3072 B covers the ~2560 B
 * working set (frame buffers, RMT encoder call stack) plus margin — see
 * memory-bank/creative/onboard-status-led-design.md "Consequences accepted". */
#define STATUS_LED_TASK_STACK_SIZE 3072
#define STATUS_LED_TASK_PRIORITY 2

/* Fixed 100 ms tick period — the poll granularity the tick task runs at.
 * This is NOT Kconfig-tunable: it's an internal implementation detail of
 * the task loop, distinct from CONFIG_HYDRO_STATUS_LED_BLINK_MS (the
 * user-tunable blink half-period, converted to a tick count below). Mirrors
 * the plain-#define-for-internal-sizing convention already used by
 * SAMPLER_TASK_STACK_SIZE/SAMPLER_TASK_PRIORITY in src/sampler.c. */
#define HYDRO_STATUS_LED_TICK_MS 100

static bool status_led_rgb_equal(status_led_rgb_t a, status_led_rgb_t b) {
    return memcmp(&a, &b, sizeof(a)) == 0;
}

static void status_led_task(void *arg) {
    (void)arg;

    uint32_t tick = 0;
    status_led_rgb_t last_shown = {0};
    bool have_shown = false;

    /* AC-ERROR-2: the first status_led_show() failure is logged immediately;
     * consecutive failures are counted but not individually logged, so a
     * stuck LED at the 10 Hz tick rate cannot flood the log. When a
     * subsequent call succeeds again, one summary line reports how many
     * were suppressed (0 suppressed => nothing extra is logged). */
    bool show_failing = false;
    uint32_t suppressed_failures = 0;

    /* blink_ticks = CONFIG_HYDRO_STATUS_LED_BLINK_MS / 100 (integer
     * division): a blink period not a multiple of the 100 ms tick
     * granularity silently rounds down (documented in the Kconfig help
     * text for HYDRO_STATUS_LED_BLINK_MS — this comment is the code-side
     * half of that same caveat). Read once: Kconfig values do not change at
     * runtime. */
    const uint32_t blink_ticks = (uint32_t)CONFIG_HYDRO_STATUS_LED_BLINK_MS / (HYDRO_STATUS_LED_TICK_MS);
    const uint8_t brightness = (uint8_t)CONFIG_HYDRO_STATUS_LED_BRIGHTNESS;

    TickType_t next_wake = xTaskGetTickCount();
    const TickType_t period_ticks = pdMS_TO_TICKS(HYDRO_STATUS_LED_TICK_MS);

    for (;;) {
        status_facts_t facts = status_snapshot();
        status_led_state_t state = status_led_core_derive_state(facts);
        status_led_rgb_t frame = status_led_core_frame(state, tick, blink_ticks, brightness);

        /* Transmit only on change: solid states settle to zero frames sent
         * per tick after the first, keeping the RMT peripheral quiet next
         * to the 1-Wire bus and bounding how often a failing LED can log
         * (AC-ERROR-2 above). */
        if (!have_shown || !status_led_rgb_equal(frame, last_shown)) {
            esp_err_t err = status_led_show(frame);
            if (err == ESP_OK) {
                if (show_failing) {
                    ESP_LOGI(TAG, "status LED recovered (%" PRIu32 " failed update(s) suppressed)",
                             suppressed_failures);
                }
                show_failing = false;
                suppressed_failures = 0;
                last_shown = frame;
                have_shown = true;
            } else {
                if (!show_failing) {
                    ESP_LOGE(TAG, "status_led_show failed: %s", esp_err_to_name(err));
                    show_failing = true;
                } else {
                    suppressed_failures++;
                }
                /* Do not update last_shown/have_shown on failure: the next
                 * tick retries the same frame rather than silently treating
                 * a failed transmit as "already shown". */
            }
        }

        tick++;
        vTaskDelayUntil(&next_wake, period_ticks);
    }
}

esp_err_t status_led_start(void) {
#ifndef CONFIG_HYDRO_STATUS_LED_ENABLE
    /* AC-VERIFY-8: clean disable. No RMT acquisition, no task, no error
     * logged — this is a supported escape hatch, not a degraded state,
     * since the onboard LED's GPIO rests on vendor Q&A rather than a
     * confirmed schematic (see the Kconfig help text). */
    return ESP_OK;
#else
    esp_err_t err = status_led_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "status_led_init failed: %s", esp_err_to_name(err));
        return err;
    }

    BaseType_t created = xTaskCreatePinnedToCore(status_led_task, "status_led", STATUS_LED_TASK_STACK_SIZE,
                                                 NULL, STATUS_LED_TASK_PRIORITY, NULL, tskNO_AFFINITY);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "failed to create status_led task (stack %d, prio %d)", STATUS_LED_TASK_STACK_SIZE,
                 STATUS_LED_TASK_PRIORITY);
        return ESP_FAIL;
    }
    return ESP_OK;
#endif
}
