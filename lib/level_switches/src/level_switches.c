#include "level_switches.h"

/**
 * Maps a fully-resolved (polarity-corrected) floating/not-floating pair to
 * the raw candidate state, per the truth table in level_switches.h.
 */
static level_switch_state_t map_floating_pair_to_state(bool high_floating, bool low_floating) {
    if (high_floating && low_floating) {
        return LEVEL_SWITCH_STATE_FULL;
    }
    if (!high_floating && low_floating) {
        return LEVEL_SWITCH_STATE_MID;
    }
    if (!high_floating && !low_floating) {
        return LEVEL_SWITCH_STATE_LOW;
    }
    /* high_floating && !low_floating: physically impossible combination. */
    return LEVEL_SWITCH_STATE_FAULT;
}

void level_switches_init(level_switches_t *sw, bool invert_high, bool invert_low) {
    sw->config.invert_high = invert_high;
    sw->config.invert_low = invert_low;
    sw->committed_state = LEVEL_SWITCH_STATE_UNKNOWN;
    sw->candidate_state = LEVEL_SWITCH_STATE_UNKNOWN;
    sw->agreement_count = 0;
}

level_switch_state_t level_switches_update(level_switches_t *sw, bool high_raw, bool low_raw) {
    bool high_floating = sw->config.invert_high ? !high_raw : high_raw;
    bool low_floating = sw->config.invert_low ? !low_raw : low_raw;
    level_switch_state_t candidate = map_floating_pair_to_state(high_floating, low_floating);

    if (candidate == sw->candidate_state) {
        if (sw->agreement_count < LEVEL_SWITCHES_DEBOUNCE_N) {
            sw->agreement_count++;
        }
    } else {
        sw->candidate_state = candidate;
        sw->agreement_count = 1;
    }

    if (sw->agreement_count >= LEVEL_SWITCHES_DEBOUNCE_N) {
        sw->committed_state = sw->candidate_state;
    }

    return sw->committed_state;
}

level_switch_state_t level_switches_state(const level_switches_t *sw) {
    return sw->committed_state;
}
