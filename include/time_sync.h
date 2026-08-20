/**
 * time_sync — device-only SNTP client + local-time (`TZ`/`tzset()`) setup.
 *
 * Not host-testable: requires the real `esp_netif`/lwip SNTP stack and a
 * real network path to an NTP server, per the Test Strategy (mirrors
 * `wifi_conn.c` having no host suite). Depends on Phase 4's Wi-Fi layer:
 * SNTP needs network to actually sync, though `time_sync_start()` itself is
 * safe to call before a connection completes — it just won't sync anything
 * until connectivity exists, exactly like `esp_wifi_connect()` scheduling.
 *
 * Why local time, not UTC: day/night is inherently a local-time concept, and
 * a planned pump-relay feature (separate roadmap item) will schedule off of
 * it. DST shifts the local day/night boundary twice a year, so a bare UTC
 * epoch is not sufficient on its own — `TZ` + `tzset()` is required so
 * `localtime()`/`mktime()`-based logic in that future feature gets the
 * right wall-clock hour.
 *
 * Why a validity predicate: the Hosyond ESP32-S3-WROOM-1 has no
 * battery-backed RTC. After a power loss with the AP unreachable, `time()`
 * reads a 1970-anchored epoch until the first successful sync — a caller
 * (the sampler, `src/sampler.c`) must be able to tell "real epoch time" from
 * "1970 because never synced" so it can set `sensor_reading_t`'s
 * `READING_VALID_TIME_BIT` correctly instead of asserting a timestamp it
 * cannot back up.
 */
#ifndef HYDROPONIC_MONITOR_TIME_SYNC_H
#define HYDROPONIC_MONITOR_TIME_SYNC_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sets the process `TZ` from Kconfig (`CONFIG_HYDRO_SNTP_TZ`) and calls
 * `tzset()`, then starts the SNTP client against the Kconfig-configured
 * server (`CONFIG_HYDRO_SNTP_SERVER`). Non-blocking: wires SNTP and returns
 * immediately: the actual sync happens asynchronously once Wi-Fi is
 * connected, and `time_sync_is_valid()` reports the result once it lands.
 *
 * Returns esp_err_t (rather than void) per the Explicit Error Handling
 * guiding principle — `esp_netif_sntp_init()` can fail (e.g. a config
 * struct rejected because CONFIG_LWIP_SNTP_MAX_SERVERS is exceeded) and that
 * must not be silently swallowed, even though it is not fatal to the rest
 * of the firmware: a failure here just means time never becomes valid,
 * which time_sync_is_valid() already reports independently of this
 * function's return.
 *
 * Call exactly once from app_main(), after wifi_conn_start().
 */
esp_err_t time_sync_start(void);

/**
 * True once at least one SNTP sync has completed successfully. False from
 * boot until then — including across the entire time the device has never
 * reached an AP, since there is no battery-backed RTC to fall back on.
 *
 * Implementation note (see src/time_sync.c): tracked primarily via the SNTP
 * time-sync notification callback, with a defensive fallback that also
 * treats any `time(NULL)` still before a small sanity threshold
 * (2020-01-01T00:00:00Z) as unsynced even if the callback state were ever
 * inconsistent — belt-and-suspenders against exactly the failure mode this
 * predicate exists to catch.
 */
bool time_sync_is_valid(void);

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_TIME_SYNC_H */
