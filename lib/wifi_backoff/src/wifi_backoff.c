/**
 * wifi_backoff — see lib/wifi_backoff/include/wifi_backoff.h for the
 * module-level design rationale (pure capped exponential-backoff sequencer).
 */
#include "wifi_backoff.h"

void wifi_backoff_reset(wifi_backoff_t *b) {
    b->next_delay_sec = WIFI_BACKOFF_FLOOR_SEC;
}

uint32_t wifi_backoff_next_delay_sec(wifi_backoff_t *b) {
    uint32_t delay = b->next_delay_sec;

    uint32_t doubled = delay * 2;
    b->next_delay_sec = (doubled > WIFI_BACKOFF_CAP_SEC) ? WIFI_BACKOFF_CAP_SEC : doubled;

    return delay;
}
