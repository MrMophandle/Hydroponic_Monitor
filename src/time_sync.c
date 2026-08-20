/**
 * time_sync — see include/time_sync.h for the module-level design rationale.
 *
 * Uses the modern `esp_netif_sntp` API (`esp_netif_sntp.h`, ESP-IDF 5.x),
 * not the older `esp_sntp_init()`/`esp_sntp_setservername()` free-function
 * style from `lwip/apps/esp_sntp.h` — the pinned ESP-IDF 5.3.1
 * (`espidf@6.9.0`) provides both, but `esp_netif_sntp_init()` takes a single
 * config struct (server, sync callback, DHCP-provided-server option) in one
 * call rather than several ordered setter calls, which is both less
 * error-prone (no missing/misordered setter) and easier to verify at a
 * glance against this module's one config block. Verified present by
 * grepping the pinned framework tree
 * (`components/esp_netif/include/esp_netif_sntp.h`) rather than assumed.
 */
#include "time_sync.h"

#include <stdlib.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"

static const char *TAG = "time_sync";

/* Set once the SNTP notification callback has fired for the first
 * successful sync. `volatile` because it is written from the callback
 * (invoked on the lwip/SNTP context) and read from any task calling
 * time_sync_is_valid() (the sampler task, in practice). */
static volatile bool s_time_valid = false;

/* Defensive fallback threshold for time_sync_is_valid(): 2020-01-01T00:00:00Z
 * as a plain Unix epoch. Any time(NULL) still below this is unambiguously
 * "never synced" on real hardware in 2026 — a belt-and-suspenders check in
 * case s_time_valid were ever left stale by a future change to the callback
 * wiring, not the primary mechanism. */
#define TIME_SYNC_SANITY_EPOCH_SEC ((time_t)1577836800)

static void time_sync_notification_cb(struct timeval *tv) {
    (void)tv;
    s_time_valid = true;
    ESP_LOGI(TAG, "SNTP time sync completed");
}

bool time_sync_is_valid(void) {
    if (s_time_valid) {
        return true;
    }
    return time(NULL) >= TIME_SYNC_SANITY_EPOCH_SEC;
}

esp_err_t time_sync_start(void) {
    /* Day/night (and DST) are local-time concepts; SNTP itself always syncs
     * UTC, so the local-time conversion is a separate, explicit step. */
    setenv("TZ", CONFIG_HYDRO_SNTP_TZ, 1);
    tzset();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_HYDRO_SNTP_SERVER);
    config.sync_cb = time_sync_notification_cb;

    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_sntp_init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "SNTP starting (server=%s, TZ=%s)", CONFIG_HYDRO_SNTP_SERVER,
             CONFIG_HYDRO_SNTP_TZ);
    return ESP_OK;
}
