#include "reading_store_core.h"

void reading_store_core_init(reading_store_core_t *store) {
    store->head = 0;
    store->count = 0;
}

void reading_store_core_push(reading_store_core_t *store, const sensor_reading_t *reading) {
    store->entries[store->head] = *reading;
    store->head = (uint16_t)((store->head + 1) % READING_STORE_CORE_CAPACITY);
    if (store->count < READING_STORE_CORE_CAPACITY) {
        store->count++;
    }
}

uint16_t reading_store_core_count(const reading_store_core_t *store) {
    return store->count;
}

uint8_t reading_store_core_is_empty(const reading_store_core_t *store) {
    return store->count == 0;
}

uint8_t reading_store_core_is_full(const reading_store_core_t *store) {
    return store->count == READING_STORE_CORE_CAPACITY;
}

/**
 * Oldest-stored index: when the ring is not yet full, entries live at
 * [0, count) so the oldest is index 0. Once full, `head` is where the next
 * write lands, i.e. also the index of the (about to be overwritten) oldest
 * entry.
 */
static uint16_t oldest_index(const reading_store_core_t *store) {
    if (store->count < READING_STORE_CORE_CAPACITY) {
        return 0;
    }
    return store->head;
}

uint16_t reading_store_core_downsample(const reading_store_core_t *store, sensor_reading_t *out,
                                        uint16_t points) {
    const uint16_t count = store->count;
    if (count == 0 || points == 0) {
        return 0;
    }

    const uint16_t n = (points < count) ? points : count;
    const uint16_t oldest = oldest_index(store);

    if (n == 1) {
        /* A single requested point is defined as the newest sample. */
        uint16_t newest_offset = (uint16_t)(count - 1);
        uint16_t idx = (uint16_t)((oldest + newest_offset) % READING_STORE_CORE_CAPACITY);
        out[0] = store->entries[idx];
        return 1;
    }

    for (uint16_t i = 0; i < n; i++) {
        /* Evenly spread i in [0, n) across the stored range [0, count-1],
         * inclusive of both ends. */
        uint32_t offset = ((uint32_t)i * (count - 1)) / (n - 1);
        uint16_t idx = (uint16_t)((oldest + offset) % READING_STORE_CORE_CAPACITY);
        out[i] = store->entries[idx];
    }
    return n;
}
