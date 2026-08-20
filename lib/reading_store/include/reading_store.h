/**
 * reading_store — thin, device-only mutex wrapper around reading_store_core.
 *
 * Owns a `SemaphoreHandle_t` mutex and delegates every ring-buffer operation
 * to `reading_store_core` (`lib/reading_store_core/`); it holds no buffer
 * arithmetic of its own. This is the split called for in the design doc:
 * `reading_store_core` is pure and host-testable (no FreeRTOS include, no
 * lock), while this module is device-only and is never compiled for
 * `[env:native]` (no host test exists for it — see systemPatterns.md § Test
 * Scope Preferences: "the wrapper is device-only and not host-tested").
 *
 * Locking discipline (per the design doc):
 *   - The writer (the sampler task, src/sampler.c) acquires with an
 *     indefinite wait (`reading_store_push()`) — it is trusted to be fast,
 *     since by the time it calls this the sensor reads have already
 *     happened outside the lock and this is just a ~20-byte copy.
 *   - Readers (HTTP handlers, landing in Phase 5) acquire with a bounded
 *     `READING_STORE_READER_TIMEOUT_MS` (100 ms) via
 *     `reading_store_downsample()` and get `ESP_ERR_TIMEOUT` rather than
 *     blocking indefinitely if the lock is contended longer than that. No
 *     reader exists yet, but the interface is fixed here because Phase 5
 *     depends on it.
 */
#ifndef HYDROPONIC_MONITOR_READING_STORE_H
#define HYDROPONIC_MONITOR_READING_STORE_H

#include "esp_err.h"
#include "reading_store_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Max time, in milliseconds, a reader blocks waiting for the store mutex before giving up. */
#define READING_STORE_READER_TIMEOUT_MS 100

/**
 * Initializes the underlying ring buffer and creates the store mutex. Must
 * be called exactly once, before any push/downsample call and before the
 * sampler task starts.
 */
void reading_store_init(void);

/**
 * Writer path: acquires the mutex (blocking indefinitely — the critical
 * section is just a ~20-byte copy into the ring) and pushes one reading.
 */
void reading_store_push(const sensor_reading_t *reading);

/**
 * Reader path: attempts to acquire the mutex within
 * READING_STORE_READER_TIMEOUT_MS. On success, downsamples the current
 * ring content into `out` (sized for at least `points` entries, capped by
 * the caller — see reading_store_core_downsample()), releases the mutex,
 * writes the actual number of entries written to `*written_out`, and
 * returns ESP_OK. On timeout, returns ESP_ERR_TIMEOUT without touching
 * `out` or `*written_out` — callers (the HTTP layer, Phase 5) are expected
 * to translate this into a 503 rather than blocking.
 */
esp_err_t reading_store_downsample(sensor_reading_t *out, uint16_t points, uint16_t *written_out);

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_READING_STORE_H */
