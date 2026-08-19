/**
 * reading_store_core — pure, host-testable ring buffer for sensor readings.
 *
 * This module owns ONLY buffer arithmetic: the static sensor_reading_t array,
 * head/count bookkeeping, wrap-and-overwrite, and downsampling into a
 * caller-supplied output buffer. It takes NO lock and includes NO FreeRTOS
 * header — concurrency control is a separate, device-only concern owned by
 * the (not-yet-built) `reading_store` wrapper in a later phase. This split
 * is what lets the buffer arithmetic compile and run in the host-only
 * [env:native] PlatformIO environment.
 *
 * Capacity: 2,880 entries = 24 hours at a 30-second sample interval.
 */
#ifndef HYDROPONIC_MONITOR_READING_STORE_CORE_H
#define HYDROPONIC_MONITOR_READING_STORE_CORE_H

#include <stdint.h>

#include "esp_shim.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Number of entries the ring buffer holds: 24h at a 30s sample interval. */
#define READING_STORE_CORE_CAPACITY 2880

/**
 * Water-level band. Five-valued by design: the four switch-combination
 * outcomes (FULL/MID/LOW/FAULT) plus UNKNOWN, the lifecycle state reported
 * before the first sample has ever completed. This module only needs the
 * enum to exist so sensor_reading_t compiles — the switch-combination state
 * machine (debounce, mapping) is a later phase's responsibility, not this
 * one's.
 */
typedef enum {
    LEVEL_UNKNOWN = 0,
    LEVEL_FULL,
    LEVEL_MID,
    LEVEL_LOW,
    LEVEL_FAULT,
} level_state_t;

/**
 * One sample. `valid` is a bitfield of per-sensor validity (bit cleared on a
 * failed read for that sensor); a failed read is never stored as 0.0 for its
 * value. Approximately 20 bytes.
 */
typedef struct {
    uint32_t uptime_sec;
    float lux;
    float temp_c;
    level_state_t level;
    uint8_t valid;
} sensor_reading_t;

/** Opaque-in-spirit ring buffer state. Callers allocate this statically. */
typedef struct {
    sensor_reading_t entries[READING_STORE_CORE_CAPACITY];
    uint16_t head;  /* index where the NEXT push will write */
    uint16_t count; /* number of valid entries currently stored, <= capacity */
} reading_store_core_t;

/** Initializes (or resets) the ring buffer to empty. */
void reading_store_core_init(reading_store_core_t *store);

/**
 * Pushes one reading into the ring. When full, overwrites the oldest entry
 * (wrap). Never fails and never allocates.
 */
void reading_store_core_push(reading_store_core_t *store, const sensor_reading_t *reading);

/** Returns the number of valid entries currently stored (0..capacity). */
uint16_t reading_store_core_count(const reading_store_core_t *store);

/** True when the store holds zero entries. */
uint8_t reading_store_core_is_empty(const reading_store_core_t *store);

/** True when the store holds exactly READING_STORE_CORE_CAPACITY entries. */
uint8_t reading_store_core_is_full(const reading_store_core_t *store);

/**
 * Fills `out` (caller-supplied, sized for at least `points` entries) with
 * `points` evenly-spaced samples spanning the store's current content,
 * oldest to newest. Never allocates and never reads outside the ring.
 *
 * - If the store is empty, writes nothing and returns 0.
 * - If `points` >= the current count, every currently-stored entry is
 *   written (in order) and the actual number written is returned — this
 *   function does NOT pad, repeat, or interpolate entries to manufacture
 *   `points` samples that don't exist.
 * - Otherwise, `points` evenly-spaced samples are selected across the
 *   currently-stored range (oldest to newest, inclusive of both ends when
 *   points > 1).
 *
 * `points` is NOT clamped to any maximum here — the caller (the HTTP layer,
 * in a later phase) is responsible for bounding it before calling.
 *
 * Returns the number of entries actually written to `out` (0..points).
 */
uint16_t reading_store_core_downsample(const reading_store_core_t *store, sensor_reading_t *out,
                                        uint16_t points);

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_READING_STORE_CORE_H */
