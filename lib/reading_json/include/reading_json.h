/**
 * reading_json — pure, host-testable JSON serializer for sensor_reading_t.
 *
 * Takes readings from the caller (a single reading for the `/api/now` shape,
 * or an array for the `/api/history` shape) and emits JSON text through a
 * caller-supplied write callback, never into a caller-supplied fixed buffer
 * and never via dynamic allocation. This is deliberate: at the `/api/history`
 * `POINTS_MAX` (500-point) worst case, materializing the whole JSON body in
 * one buffer before sending it would need tens of KB of RAM on top of the
 * downsample snapshot that already holds the same 500 readings — the
 * callback lets the device-only caller (http_api.c) stream each fragment
 * straight into `httpd_resp_send_chunk()` as it is produced, so peak RAM
 * stays flat regardless of how many points are requested.
 *
 * Pure half of the Pure-Logic / Device-Only Split (systemPatterns.md):
 * no FreeRTOS header, no `httpd_*`, no dynamic allocation. Host-testable
 * under `[env:native]`.
 *
 * CROSS-HALF CONTRACT (this module's assumptions the device-only caller must
 * honor — see systemPatterns.md § Pure-Logic/Device-Only Split → "the split
 * hides cross-half contracts"):
 *   1. `write_cb` may be called many times per `reading_json_write_now()` /
 *      `reading_json_write_history()` invocation, each with a small,
 *      arbitrarily-sized fragment of the overall JSON text. Fragment
 *      boundaries never split a byte's *meaning* (every byte delivered is
 *      exactly the JSON text, in order) — they only determine how many
 *      callback invocations it takes to deliver it. **This module never
 *      buffers to guarantee a fragment lands on a token boundary**; the
 *      caller must simply forward/append bytes in the order and content
 *      given, never reorder, drop, or duplicate a fragment.
 *   2. Both entry points are synchronous and single-threaded: they drive the
 *      callback directly on the calling stack until serialization completes
 *      or the callback reports an error. The caller must not call either
 *      entry point reentrantly (e.g. from two concurrent HTTP requests)
 *      against a callback/context pair that is not itself safe for
 *      concurrent use.
 *   3. The RESPONSIBLE device-only module for honoring both of the above is
 *      `src/http_api.c`: its write-callback adapter must call
 *      `httpd_resp_send_chunk()` synchronously, in order, for each fragment,
 *      and must not interleave writes from a second in-flight request into
 *      the same response.
 *
 * Numeric formatting (fixed here so tests and callers agree without needing
 * to inspect output): `lux` and `temp_c` are formatted with two decimal
 * places (`%.2f`) when valid. `epoch_sec` is formatted as a plain decimal
 * unsigned integer, always present (never `null`) — `time_valid` is the
 * separate, explicit signal for whether it should be trusted; a caller that
 * ignores `time_valid` and reads `t` anyway still gets a well-formed number,
 * just possibly a small (pre-sync) one.
 */
#ifndef HYDROPONIC_MONITOR_READING_JSON_H
#define HYDROPONIC_MONITOR_READING_JSON_H

#include <stddef.h>
#include <stdint.h>

#include "reading_store_core.h"

#ifndef ESP_PLATFORM
#include "esp_shim.h"
#else
#include "esp_err.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Receives one contiguous fragment of JSON text (`chunk`, `len` bytes — NOT
 * null-terminated, and may be zero-length though this module never emits a
 * zero-length fragment). Return ESP_OK to keep serializing; any other
 * esp_err_t aborts serialization immediately and is propagated back to the
 * caller of reading_json_write_now()/reading_json_write_history() unchanged.
 */
typedef esp_err_t (*reading_json_write_cb_t)(const char *chunk, size_t len, void *ctx);

/**
 * Serializes one reading in the `/api/now` shape:
 *   {"t":<epoch_sec>,"time_valid":<bool>,"lux":<float|null>,
 *    "temp_c":<float|null>,"level":"<FULL|MID|LOW|FAULT|UNKNOWN>",
 *    "valid":{"light":<bool>,"temp":<bool>,"level":<bool>}}
 *
 * `lux`/`temp_c` serialize as JSON `null` (never `0`) when their respective
 * validity bit (READING_VALID_LIGHT_BIT / READING_VALID_TEMP_BIT) is clear —
 * per AC-HAPPY-2. `level` is always a quoted band name, never a bare number.
 *
 * @return ESP_OK on success, or the first non-ESP_OK value `write_cb`
 *         returned (serialization stops at that point).
 */
esp_err_t reading_json_write_now(const sensor_reading_t *reading, reading_json_write_cb_t write_cb,
                                  void *ctx);

/**
 * Serializes `count` readings (oldest to newest, as given — this module does
 * not reorder or downsample) in the `/api/history` parallel-array shape:
 *   {"t":[...],"time_valid":[...],"lux":[...],"temp_c":[...],"level":[...]}
 *
 * All five arrays are always exactly `count` elements long and index-aligned
 * — array index i across every array describes the same reading,
 * `readings[i]`. `count == 0` serializes as five well-formed empty arrays,
 * not an error. `lux`/`temp_c` entries serialize as `null` per-entry when
 * that reading's validity bit is clear (same rule as reading_json_write_now).
 *
 * @return ESP_OK on success, or the first non-ESP_OK value `write_cb`
 *         returned (serialization stops at that point).
 */
esp_err_t reading_json_write_history(const sensor_reading_t *readings, uint16_t count,
                                      reading_json_write_cb_t write_cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_READING_JSON_H */
