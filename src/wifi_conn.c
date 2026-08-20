/**
 * wifi_conn — see include/wifi_conn.h for the module-level design rationale.
 *
 * Reconnect scheduling: a single one-shot `esp_timer` (not a dedicated
 * FreeRTOS task) fires `esp_wifi_connect()` after the backoff delay. This is
 * the simpler of the two options named in the roadmap task (esp_timer vs. a
 * vTaskDelay task): esp_timer already runs its callback on the system
 * "Tmr Svc"/esp_timer task, so no extra task, stack, or teardown logic is
 * needed here — the callback body is a single non-blocking call.
 *
 * mDNS: registered (or re-registered) on every IP_EVENT_STA_GOT_IP, per
 * AC-ENTRY-2 — a DHCP lease change or reboot must not require re-flashing.
 * `mdns_init()` is idempotent in the underlying component but is still
 * checked: ESP_ERR_INVALID_STATE from a second init is treated as fine,
 * anything else is logged as a genuine failure and mDNS is skipped for that
 * connection (the serial-logged IP from AC-ERROR-7 remains the fallback).
 */
#include "wifi_conn.h"

#include <inttypes.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "mdns.h"

#include "wifi_backoff.h"

/* AC-ERROR-6: a bare `#include "wifi_secrets.h"` on a missing header would
 * emit only the compiler's own "no such file or directory", which names the
 * missing file but not the example to copy or what to do. This guard fails
 * loudly and names both.
 *
 * cppcheck false positive (verified, not a defect): under `pio check`'s full
 * fixed-configuration invocation (every GCC/xtensa built-in -D supplied
 * explicitly, so cppcheck skips its usual multi-configuration #ifdef
 * exploration), cppcheck's own `__has_include` evaluation fails to locate
 * `wifi_secrets.h` even though the correct `include/` search directory is
 * present in its `-I`/`--includes-file` list — confirmed by replaying the
 * exact captured cppcheck invocation with only the `-D` list stripped, which
 * resolves the header correctly. The real IDF/GCC build (`pio run`) always
 * finds the file and succeeds; this is specific to cppcheck's preprocessor
 * under that flag combination. Suppressed at the point of use rather than
 * globally, so a genuine future preprocessorErrorDirective elsewhere is not
 * hidden. */
// cppcheck-suppress preprocessorErrorDirective
#if !__has_include("wifi_secrets.h")
#error "Missing include/wifi_secrets.h — copy include/wifi_secrets.h.example to include/wifi_secrets.h and fill in your SSID and password. It is gitignored on purpose."
#endif
#include "wifi_secrets.h"

static const char *TAG = "wifi_conn";

#define WIFI_CONN_MDNS_HOSTNAME "hydroponics"
#define WIFI_CONN_MDNS_INSTANCE_NAME "Hydroponic Monitor"

/* Module-static: owned entirely by this file, per the design (the pure
 * arithmetic lives in lib/wifi_backoff/; only the device-only glue —
 * scheduling the timer and calling esp_wifi_connect() — lives here). */
static wifi_backoff_t s_backoff;
static esp_timer_handle_t s_reconnect_timer = NULL;

/* Fires once, after the current backoff delay has elapsed. Runs on the
 * esp_timer service task, not the event-loop task, so it is fine for this to
 * make the (non-blocking) esp_wifi_connect() call directly. */
static void reconnect_timer_callback(void *arg) {
    (void)arg;
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect() (scheduled reconnect) failed: %s", esp_err_to_name(err));
    }
}

/* Registers (or re-registers) mDNS so the hostname keeps resolving across
 * reboots and DHCP lease changes (AC-ENTRY-2). Safe to call on every
 * successful connection, not just the first. */
static void wifi_conn_register_mdns(void) {
    esp_err_t err = mdns_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(err));
        return;
    }

    err = mdns_hostname_set(WIFI_CONN_MDNS_HOSTNAME);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_hostname_set failed: %s", esp_err_to_name(err));
    }

    err = mdns_instance_name_set(WIFI_CONN_MDNS_INSTANCE_NAME);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_instance_name_set failed: %s", esp_err_to_name(err));
    }
}

/* Schedules a reconnect after the next capped-exponential-backoff delay
 * (AC-ERROR-3). Never blocks the caller (the event-loop task). */
static void wifi_conn_schedule_reconnect(void) {
    uint32_t delay_sec = wifi_backoff_next_delay_sec(&s_backoff);

    /* A disconnect while a previous reconnect attempt is still pending
     * (e.g. rapid flapping) should reschedule from the fresh delay rather
     * than let a stale timer fire early — stop first, ignoring
     * ESP_ERR_INVALID_STATE for "wasn't running". */
    esp_timer_stop(s_reconnect_timer);

    esp_err_t err = esp_timer_start_once(s_reconnect_timer, (uint64_t)delay_sec * 1000000ULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to schedule reconnect in %" PRIu32 " s: %s", delay_sec,
                 esp_err_to_name(err));
        return;
    }
    ESP_LOGW(TAG, "disconnected — reconnecting in %" PRIu32 " s", delay_sec);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                                void *event_data) {
    (void)arg;
    (void)event_data;

    if (event_base != WIFI_EVENT) {
        return;
    }

    if (event_id == WIFI_EVENT_STA_START) {
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_connect() (initial) failed: %s", esp_err_to_name(err));
        }
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        /* AC-ERROR-3: only the delivery channel is down — sampling
         * continues untouched (sampler.c has no dependency on this file).
         * We just schedule the next connect attempt on backoff. */
        wifi_conn_schedule_reconnect();
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                              void *event_data) {
    (void)arg;

    if (event_base != IP_EVENT || event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    /* A successful connection resets the backoff sequence back to the
     * floor, per AC-ERROR-3 ("... and re-registers mDNS; and when the
     * connection returns ..."). */
    wifi_backoff_reset(&s_backoff);
    wifi_conn_register_mdns();

    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    /* AC-ERROR-7: unambiguous, greppable, logged on every connect AND every
     * subsequent reconnect — both the raw-IP URL and the mDNS URL in one
     * line, since mDNS resolution is not reliable on every client. */
    ESP_LOGI(TAG, "connected — dashboard at http://" IPSTR "/ (mDNS: http://" WIFI_CONN_MDNS_HOSTNAME ".local/)",
             IP2STR(&event->ip_info.ip));
}

/* Initializes NVS if it has not already been initialized anywhere else in
 * the codebase (nothing in src/ or lib/ calls nvs_flash_init() as of Phase
 * 3 — grepped before writing this). Standard IDF erase-and-retry pattern for
 * the two recoverable failure codes. */
static void wifi_conn_init_nvs(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase (%s); erasing and retrying",
                 esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void wifi_conn_start(void) {
    wifi_backoff_reset(&s_backoff);

    wifi_conn_init_nvs();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                          &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                          &ip_event_handler, NULL, NULL));

    const esp_timer_create_args_t timer_args = {
        .callback = &reconnect_timer_callback,
        .arg = NULL,
        .name = "wifi_reconnect",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_reconnect_timer));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "station mode started, connecting to configured AP");
}
