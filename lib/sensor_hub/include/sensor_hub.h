/**
 * sensor_hub — orchestrates one sample cycle across the three sensors and
 * tracks per-sensor consecutive-failure escalation.
 *
 * This module is pure, host-testable logic: it holds NO driver handles
 * (no `bh1750_t`, no `ds18b20_probe_t`, no I2C/1-Wire/GPIO state) and
 * includes none of the real drivers' headers, because those headers
 * themselves pull in real ESP-IDF driver headers (`driver/i2c_master.h`,
 * `onewire_bus.h`, ...) that do not exist under the host-only [env:native]
 * environment. Instead, `sensor_hub.c` calls three small `extern`
 * "read one sensor" functions declared below — `sensor_hub_light_read()`,
 * `sensor_hub_temp_read()`, `sensor_hub_level_read()` — whose *bodies* are
 * supplied by whichever translation unit is linked in:
 *
 *   - Device build: the real bodies live in `src/sampler.c`, which owns the
 *     actual `bh1750_t`/`ds18b20_probe_t`/`level_switches_t` driver handles
 *     and calls the real `bh1750_read_lux()` / `ds18b20_probe_read_temp_c()`
 *     / GPIO-sampled `level_switches_update()` under the hood.
 *   - `[env:native]` test build: `test/native/stubs/{bh1750,ds18b20_probe,
 *     level_switches}_stub.c` provide test-controllable bodies (see
 *     `test/native/stubs/sensor_hub_stubs.h`).
 *
 * This is the same "link-time substitution" pattern used elsewhere in this
 * project (systemPatterns.md § Testing Patterns), applied at the level of
 * three plain function symbols rather than a whole driver library, which is
 * what keeps this header (and `sensor_hub.c`) free of any ESP-IDF
 * dependency at all.
 *
 * Failure handling (per the design doc): a non-OK return from a sensor's
 * read function clears that sensor's `valid` bit for the cycle — nothing
 * aborts, the other sensors still record. A per-sensor consecutive-failure
 * counter escalates to "offline" after SENSOR_HUB_OFFLINE_THRESHOLD (5)
 * straight failures; a single successful read clears both the offline flag
 * and the counter.
 */
#ifndef HYDROPONIC_MONITOR_SENSOR_HUB_H
#define HYDROPONIC_MONITOR_SENSOR_HUB_H

#include <stdbool.h>
#include <stdint.h>

#include "level_switches.h"

/* level_switches.h has zero ESP-IDF dependency, so it is safe to include
 * unconditionally on both the device and [env:native] builds. `esp_err_t`
 * is not — the real device build gets it from ESP-IDF's own esp_err.h
 * (ESP_PLATFORM is defined by the ESP-IDF build system), while the host
 * build gets it (plus the no-op ESP_LOG* macros used in sensor_hub.c) from
 * the native shim. */
#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "esp_log.h"
#else
#include "esp_shim.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Consecutive read failures after which a sensor is escalated to "offline". */
#define SENSOR_HUB_OFFLINE_THRESHOLD 5

/** Per-sensor identity, used to index the failure-tracking arrays in sensor_hub_t. */
typedef enum {
    SENSOR_HUB_SENSOR_LIGHT = 0,
    SENSOR_HUB_SENSOR_TEMP,
    SENSOR_HUB_SENSOR_LEVEL,
    SENSOR_HUB_SENSOR_COUNT,
} sensor_hub_sensor_id_t;

/**
 * Result of one sample cycle. A `false` valid flag means the corresponding
 * value field MUST be ignored by the caller — it is never a meaningful 0.0
 * or a stale prior value (per Decision 5 in the design doc: "failed reads
 * are stored as invalid, never as 0").
 */
typedef struct {
    float lux;
    float temp_c;
    level_switch_state_t level;
    bool light_valid;
    bool temp_valid;
    bool level_valid;
} sensor_hub_reading_t;

/** Per-sensor failure-escalation state. Callers allocate this and pass it to every call. */
typedef struct {
    uint8_t consecutive_failures[SENSOR_HUB_SENSOR_COUNT];
    bool offline[SENSOR_HUB_SENSOR_COUNT];
} sensor_hub_t;

/** Initializes (or resets) all per-sensor failure/offline tracking to zero/false. */
void sensor_hub_init(sensor_hub_t *hub);

/**
 * Runs one sample cycle: calls each of the three sensor read functions
 * exactly once, updates `hub`'s per-sensor failure counters and offline
 * flags, and returns the cycle's reading. Never aborts partway — a failure
 * on one sensor does not prevent the other two from being read and
 * recorded.
 */
sensor_hub_reading_t sensor_hub_run_cycle(sensor_hub_t *hub);

/** True if `sensor` has been escalated to offline (>= SENSOR_HUB_OFFLINE_THRESHOLD
 * consecutive failures since its last successful read). */
bool sensor_hub_is_offline(const sensor_hub_t *hub, sensor_hub_sensor_id_t sensor);

/** Current consecutive-failure count for `sensor` (0 immediately after a successful read). */
uint8_t sensor_hub_consecutive_failures(const sensor_hub_t *hub, sensor_hub_sensor_id_t sensor);

/**
 * The three per-sensor "read one sample" seams. Declared here, defined
 * elsewhere (device: src/sampler.c; native test: test/native/stubs/) — see
 * the file-level comment above.
 */
esp_err_t sensor_hub_light_read(float *lux_out);
esp_err_t sensor_hub_temp_read(float *temp_c_out);
esp_err_t sensor_hub_level_read(level_switch_state_t *level_out);

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_SENSOR_HUB_H */
