/**
 * level_switches — pure, host-testable three-band water-level state machine.
 *
 * Takes the two raw float-switch booleans (as sampled from GPIO by a caller —
 * GPIO reading itself is out of scope for this module and belongs to a thin
 * device-only caller built in a later phase, e.g. sensor_hub) and resolves
 * them to a debounced level_switch_state_t. This module has ZERO FreeRTOS or
 * ESP-IDF-hardware dependency (no gpio_* calls, no esp_shim.h) so it compiles
 * and runs under the host-only [env:native] PlatformIO environment, matching
 * the reading_store_core split-module pattern from Phase 1.
 *
 * Truth table (after resolving each switch's raw electrical reading to its
 * logical "floating" value via the configured polarity):
 *
 *   high floating | low floating | state
 *   --------------|--------------|-------
 *   floating      | floating     | FULL   (at or above the high mark)
 *   not floating  | floating     | MID    (between the marks)
 *   not floating  | not floating | LOW    (below the low mark)
 *   floating      | not floating | FAULT  (physically impossible; reported,
 *                                          never resolved to a neighboring
 *                                          band)
 *
 * Debounce: a state change is only committed once LEVEL_SWITCHES_DEBOUNCE_N
 * consecutive samples agree on the same candidate state. A flapping/
 * alternating sequence that never holds for N consecutive samples never
 * commits a change. N=3 is chosen as a small, deliberate value: float
 * switches can chatter for a sample or two as the water surface moves, but a
 * genuine level transition holds steady long enough to be sampled 3 times in
 * a row at the caller's sampling cadence.
 *
 * Polarity: float switches are physically reversible (mounting can swap
 * normally-open/closed), so raw "GPIO reads high" does NOT always mean
 * "floating" — this is configured per-switch at init via invert_high /
 * invert_low rather than hardcoded, per the "Configuration Is Not
 * Hard-Coded" principle.
 */
#ifndef HYDROPONIC_MONITOR_LEVEL_SWITCHES_H
#define HYDROPONIC_MONITOR_LEVEL_SWITCHES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Number of consecutive agreeing samples required to commit a state change. */
#define LEVEL_SWITCHES_DEBOUNCE_N 3

/**
 * Resolved water-level state. UNKNOWN is the lifecycle value reported before
 * the very first debounce window has completed (i.e. before
 * LEVEL_SWITCHES_DEBOUNCE_N consecutive agreeing samples have ever been
 * seen).
 */
typedef enum {
    LEVEL_SWITCH_STATE_UNKNOWN = 0,
    LEVEL_SWITCH_STATE_FULL,
    LEVEL_SWITCH_STATE_MID,
    LEVEL_SWITCH_STATE_LOW,
    LEVEL_SWITCH_STATE_FAULT,
} level_switch_state_t;

/** Per-switch polarity/inversion state, set once at init from Kconfig/NVS. */
typedef struct {
    bool invert_high; /* true if raw GPIO HIGH means "not floating" for the high switch */
    bool invert_low;  /* true if raw GPIO HIGH means "not floating" for the low switch */
} level_switches_config_t;

/** Debounce state. Callers allocate this statically or on the stack. */
typedef struct {
    level_switches_config_t config;
    level_switch_state_t committed_state; /* last committed (debounced) state */
    level_switch_state_t candidate_state; /* most recent raw-mapped state under evaluation */
    uint8_t agreement_count;              /* consecutive samples agreeing with candidate_state */
} level_switches_t;

/** Initializes the state machine. committed_state starts at UNKNOWN. */
void level_switches_init(level_switches_t *sw, bool invert_high, bool invert_low);

/**
 * Feeds one raw sample pair (as read from GPIO, pre-inversion) into the
 * debounce state machine and returns the current committed state.
 *
 * `high_raw` / `low_raw` are the raw electrical/GPIO readings, NOT yet
 * corrected for polarity — this function applies invert_high/invert_low
 * internally before mapping to a candidate state.
 */
level_switch_state_t level_switches_update(level_switches_t *sw, bool high_raw, bool low_raw);

/** Returns the current committed state without sampling. */
level_switch_state_t level_switches_state(const level_switches_t *sw);

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_LEVEL_SWITCHES_H */
