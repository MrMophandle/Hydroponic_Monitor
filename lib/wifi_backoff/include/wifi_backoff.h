/**
 * wifi_backoff — pure, host-testable capped exponential-backoff delay
 * sequencer for Wi-Fi reconnect attempts.
 *
 * This module owns ONLY the backoff arithmetic: what delay (in seconds) to
 * sleep before the NEXT reconnect attempt, and how that delay advances on
 * repeated failures. It takes NO FreeRTOS/ESP-IDF dependency (no vTaskDelay,
 * no esp_timer, no esp_wifi/esp_event calls) so it compiles and runs under
 * the host-only [env:native] PlatformIO environment, matching the
 * reading_store_core / level_switches split-module pattern from earlier
 * phases. The device-only half — actually sleeping for the returned delay
 * and calling esp_wifi_connect() again — lives in the non-host-testable
 * `src/wifi_conn.c` (Phase 4), which owns a `wifi_backoff_t` instance and
 * delegates all arithmetic here.
 *
 * Sequence, per AC-ERROR-3 / the creative doc's Reconnection design:
 *   1 -> 2 -> 4 -> 8 -> 16 -> 30 (capped; the next uncapped double, 32, is
 *   clamped to 30) -> stays at 30 on every subsequent call until
 *   wifi_backoff_reset() is called again (e.g. on a successful connection,
 *   IP_EVENT_STA_GOT_IP).
 *
 * The 1 s floor and 30 s cap are spec-fixed by AC-ERROR-3, not environment
 * configuration, so — per the same reasoning that justified hardcoding the
 * BH1750 datasheet timing constants in Phase 2 — they are hardcoded here
 * rather than exposed as Kconfig values.
 */
#ifndef HYDROPONIC_MONITOR_WIFI_BACKOFF_H
#define HYDROPONIC_MONITOR_WIFI_BACKOFF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Delay floor, in seconds, returned by the first call after a reset. */
#define WIFI_BACKOFF_FLOOR_SEC 1

/** Delay ceiling, in seconds, that the sequence never exceeds. */
#define WIFI_BACKOFF_CAP_SEC 30

/** Opaque-in-spirit backoff sequencer state. Callers allocate this statically. */
typedef struct {
    uint32_t next_delay_sec;
} wifi_backoff_t;

/**
 * Resets the sequencer so the next call to wifi_backoff_next_delay_sec()
 * returns the floor (WIFI_BACKOFF_FLOOR_SEC) again. Call this on every
 * successful connection.
 */
void wifi_backoff_reset(wifi_backoff_t *b);

/**
 * Returns the delay, in seconds, to sleep before the NEXT reconnect
 * attempt, then advances the internal state by doubling (capped at
 * WIFI_BACKOFF_CAP_SEC).
 */
uint32_t wifi_backoff_next_delay_sec(wifi_backoff_t *b);

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_WIFI_BACKOFF_H */
