/**
 * reading_json — see include/reading_json.h for the module-level contract.
 *
 * Implementation note: every field/element is emitted via its own
 * `emit()`/`emit_snprintf()` call, i.e. one `write_cb` invocation per JSON
 * token (or small group of literal punctuation). This is deliberate, not
 * accidental granularity — it is what actually exercises the "chunk
 * boundaries never corrupt the concatenated output" contract in the test
 * suite, and it means this module never needs a staging buffer sized for a
 * whole response (see the RAM rationale in the header).
 */
#include "reading_json.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/** Longest single formatted fragment this module ever produces (a float via
 * "%.2f" or a uint32_t decimal) comfortably fits in 32 bytes. */
#define READING_JSON_FRAGMENT_BUF_SIZE 32

static esp_err_t emit(reading_json_write_cb_t write_cb, void *ctx, const char *s) {
    return write_cb(s, strlen(s), ctx);
}

/* printf-style helper: formats into a small stack buffer, then emits it.
 * Only ever used for the fixed, known-short numeric fragments documented in
 * the header (float with 2 decimals, or a uint32_t decimal) — never for
 * anything whose length depends on unbounded caller input. */
static esp_err_t emit_fmt(reading_json_write_cb_t write_cb, void *ctx, const char *fmt, ...) {
    char buf[READING_JSON_FRAGMENT_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof(buf)) {
        /* Would only happen if a future field format grows past the fixed
         * fragment buffer — a programming error in this module, not a
         * runtime/input condition, so it is reported rather than silently
         * truncated. */
        return ESP_FAIL;
    }
    return write_cb(buf, (size_t)n, ctx);
}

static const char *level_json_name(level_state_t level) {
    switch (level) {
        case LEVEL_FULL:
            return "FULL";
        case LEVEL_MID:
            return "MID";
        case LEVEL_LOW:
            return "LOW";
        case LEVEL_FAULT:
            return "FAULT";
        case LEVEL_UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

/** Emits a float value, or JSON `null` when `valid_bit` is clear in
 * `valid` — the single point that enforces AC-HAPPY-2 ("never 0"). */
static esp_err_t emit_float_or_null(reading_json_write_cb_t write_cb, void *ctx, float value,
                                     uint8_t valid, uint8_t valid_bit) {
    if ((valid & valid_bit) == 0) {
        return emit(write_cb, ctx, "null");
    }
    return emit_fmt(write_cb, ctx, "%.2f", (double)value);
}

static esp_err_t emit_bool(reading_json_write_cb_t write_cb, void *ctx, bool value) {
    return emit(write_cb, ctx, value ? "true" : "false");
}

esp_err_t reading_json_write_now(const sensor_reading_t *reading, reading_json_write_cb_t write_cb,
                                  void *ctx) {
    esp_err_t err;

    if ((err = emit(write_cb, ctx, "{\"t\":")) != ESP_OK) return err;
    if ((err = emit_fmt(write_cb, ctx, "%u", (unsigned)reading->epoch_sec)) != ESP_OK) return err;

    if ((err = emit(write_cb, ctx, ",\"time_valid\":")) != ESP_OK) return err;
    if ((err = emit_bool(write_cb, ctx, (reading->valid & READING_VALID_TIME_BIT) != 0)) != ESP_OK)
        return err;

    if ((err = emit(write_cb, ctx, ",\"lux\":")) != ESP_OK) return err;
    if ((err = emit_float_or_null(write_cb, ctx, reading->lux, reading->valid,
                                   READING_VALID_LIGHT_BIT)) != ESP_OK)
        return err;

    if ((err = emit(write_cb, ctx, ",\"temp_c\":")) != ESP_OK) return err;
    if ((err = emit_float_or_null(write_cb, ctx, reading->temp_c, reading->valid,
                                   READING_VALID_TEMP_BIT)) != ESP_OK)
        return err;

    if ((err = emit(write_cb, ctx, ",\"level\":\"")) != ESP_OK) return err;
    if ((err = emit(write_cb, ctx, level_json_name(reading->level))) != ESP_OK) return err;
    if ((err = emit(write_cb, ctx, "\"")) != ESP_OK) return err;

    if ((err = emit(write_cb, ctx, ",\"valid\":{\"light\":")) != ESP_OK) return err;
    if ((err = emit_bool(write_cb, ctx, (reading->valid & READING_VALID_LIGHT_BIT) != 0)) != ESP_OK)
        return err;
    if ((err = emit(write_cb, ctx, ",\"temp\":")) != ESP_OK) return err;
    if ((err = emit_bool(write_cb, ctx, (reading->valid & READING_VALID_TEMP_BIT) != 0)) != ESP_OK)
        return err;
    if ((err = emit(write_cb, ctx, ",\"level\":")) != ESP_OK) return err;
    if ((err = emit_bool(write_cb, ctx, (reading->valid & READING_VALID_LEVEL_BIT) != 0)) != ESP_OK)
        return err;
    if ((err = emit(write_cb, ctx, "}}")) != ESP_OK) return err;

    return ESP_OK;
}

/* Emits one array's worth of a given field across `readings[0..count)`,
 * via a caller-supplied per-element emitter, comma-separated and bracketed.
 * Shared by every array in reading_json_write_history() so the
 * comma/bracket bookkeeping exists exactly once. */
typedef esp_err_t (*element_emit_fn_t)(reading_json_write_cb_t write_cb, void *ctx,
                                        const sensor_reading_t *reading);

static esp_err_t emit_array(reading_json_write_cb_t write_cb, void *ctx,
                             const sensor_reading_t *readings, uint16_t count,
                             element_emit_fn_t emit_element) {
    esp_err_t err;
    if ((err = emit(write_cb, ctx, "[")) != ESP_OK) return err;
    for (uint16_t i = 0; i < count; i++) {
        if (i > 0) {
            if ((err = emit(write_cb, ctx, ",")) != ESP_OK) return err;
        }
        if ((err = emit_element(write_cb, ctx, &readings[i])) != ESP_OK) return err;
    }
    return emit(write_cb, ctx, "]");
}

static esp_err_t emit_t_element(reading_json_write_cb_t write_cb, void *ctx,
                                 const sensor_reading_t *reading) {
    return emit_fmt(write_cb, ctx, "%u", (unsigned)reading->epoch_sec);
}

static esp_err_t emit_time_valid_element(reading_json_write_cb_t write_cb, void *ctx,
                                          const sensor_reading_t *reading) {
    return emit_bool(write_cb, ctx, (reading->valid & READING_VALID_TIME_BIT) != 0);
}

static esp_err_t emit_lux_element(reading_json_write_cb_t write_cb, void *ctx,
                                   const sensor_reading_t *reading) {
    return emit_float_or_null(write_cb, ctx, reading->lux, reading->valid, READING_VALID_LIGHT_BIT);
}

static esp_err_t emit_temp_c_element(reading_json_write_cb_t write_cb, void *ctx,
                                      const sensor_reading_t *reading) {
    return emit_float_or_null(write_cb, ctx, reading->temp_c, reading->valid,
                               READING_VALID_TEMP_BIT);
}

static esp_err_t emit_level_element(reading_json_write_cb_t write_cb, void *ctx,
                                     const sensor_reading_t *reading) {
    esp_err_t err;
    if ((err = emit(write_cb, ctx, "\"")) != ESP_OK) return err;
    if ((err = emit(write_cb, ctx, level_json_name(reading->level))) != ESP_OK) return err;
    return emit(write_cb, ctx, "\"");
}

esp_err_t reading_json_write_history(const sensor_reading_t *readings, uint16_t count,
                                      reading_json_write_cb_t write_cb, void *ctx) {
    esp_err_t err;

    if ((err = emit(write_cb, ctx, "{\"t\":")) != ESP_OK) return err;
    if ((err = emit_array(write_cb, ctx, readings, count, emit_t_element)) != ESP_OK) return err;

    if ((err = emit(write_cb, ctx, ",\"time_valid\":")) != ESP_OK) return err;
    if ((err = emit_array(write_cb, ctx, readings, count, emit_time_valid_element)) != ESP_OK)
        return err;

    if ((err = emit(write_cb, ctx, ",\"lux\":")) != ESP_OK) return err;
    if ((err = emit_array(write_cb, ctx, readings, count, emit_lux_element)) != ESP_OK) return err;

    if ((err = emit(write_cb, ctx, ",\"temp_c\":")) != ESP_OK) return err;
    if ((err = emit_array(write_cb, ctx, readings, count, emit_temp_c_element)) != ESP_OK)
        return err;

    if ((err = emit(write_cb, ctx, ",\"level\":")) != ESP_OK) return err;
    if ((err = emit_array(write_cb, ctx, readings, count, emit_level_element)) != ESP_OK)
        return err;

    return emit(write_cb, ctx, "}");
}
