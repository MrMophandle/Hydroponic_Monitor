#include "device_status.h"

#include <stdatomic.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#else
#include "esp_shim.h"
#endif

static const char *TAG = "device_status";

/* Each fact defaults to STATUS_FACT_UNKNOWN (enum value 0) with no explicit
 * init needed — this is exactly the tri-state "boot window is honest"
 * property the Phase 1 design exists to preserve: before either fact has
 * been reported, the derived state is RED_BLINK, not a falsely-permanent
 * RED_SOLID. */
static atomic_int s_wifi_fact;
static atomic_int s_http_fact;

/* Sentinel outside status_led_state_t's valid range (0-2), so the very
 * first report always logs regardless of which state it derives to. */
static atomic_int s_last_logged_state = -1;

/* Re-derives the combined LED state from the current facts and logs at
 * ESP_LOGI only when it differs from the last logged state (log-on-change),
 * mirroring the transmit-on-change convention used by status_led_show() in
 * Phase 3 of this same task. Shared by status_report_wifi()/_http() so the
 * log-on-change bookkeeping lives in exactly one place. */
static void status_report_log_on_change(void) {
    status_facts_t facts = {
        .wifi = (status_fact_t)atomic_load(&s_wifi_fact),
        .http = (status_fact_t)atomic_load(&s_http_fact),
    };
    status_led_state_t derived = status_led_core_derive_state(facts);

    int previous = atomic_exchange(&s_last_logged_state, (int)derived);
    if (previous != (int)derived) {
        ESP_LOGI(TAG, "led state -> %d (wifi=%d http=%d)", (int)derived, (int)facts.wifi,
                 (int)facts.http);
    }
}

void status_report_wifi(status_fact_t fact) {
    atomic_store(&s_wifi_fact, (int)fact);
    status_report_log_on_change();
}

void status_report_http(status_fact_t fact) {
    atomic_store(&s_http_fact, (int)fact);
    status_report_log_on_change();
}

status_facts_t status_snapshot(void) {
    /* Two independent atomic loads, no lock: each fact has exactly one
     * writer in production (wifi_conn.c's event handlers; main.c's
     * http_api_start() call site) and there is no read-modify-write across
     * the pair, so atomic_int alone is sufficient — no mutex needed. See
     * memory-bank/creative/onboard-status-led-design.md Decision 5. */
    status_facts_t facts = {
        .wifi = (status_fact_t)atomic_load(&s_wifi_fact),
        .http = (status_fact_t)atomic_load(&s_http_fact),
    };
    return facts;
}

void status_set(device_status_t status) {
    switch (status) {
        case DEVICE_STATUS_OK:
            ESP_LOGI(TAG, "status: OK");
            break;
        case DEVICE_STATUS_WIFI_DOWN:
            ESP_LOGW(TAG, "status: WIFI_DOWN");
            break;
        case DEVICE_STATUS_SENSOR_FAULT:
            ESP_LOGE(TAG, "status: SENSOR_FAULT");
            break;
        case DEVICE_STATUS_LEVEL_FAULT:
            ESP_LOGE(TAG, "status: LEVEL_FAULT");
            break;
        default:
            ESP_LOGW(TAG, "status: UNKNOWN (%d)", (int)status);
            break;
    }
}
