/**
 * test_reading_json — Unity suite for the pure reading_json serializer.
 * Runs entirely on the host ([env:native]); no FreeRTOS, no board, no
 * httpd_* — reading_json never calls either.
 *
 * Deliberately NOT tested here: `?points=` clamping (that is http_api.c's
 * job, not this module's — see reading_json.h) and actual esp_http_server
 * chunked-send behavior (device-only, no host harness; verified manually
 * with `curl` per the Test Strategy).
 */
#include <string.h>

#include <unity.h>

#include "reading_json.h"
#include "reading_store_core.h"

/* Accumulates every fragment delivered to the write callback into one
 * contiguous buffer, in order, so tests can assert on the fully-reassembled
 * JSON text regardless of how many callback invocations it took to deliver
 * it — this is what actually exercises the "chunk boundaries never corrupt
 * the concatenated output" contract, since the module is free to (and does)
 * call back once per field rather than once for the whole payload. */
typedef struct {
    char buf[4096];
    size_t len;
    int call_count;
} accumulator_t;

static void accumulator_reset(accumulator_t *acc) {
    acc->len = 0;
    acc->call_count = 0;
    acc->buf[0] = '\0';
}

static esp_err_t accumulate_cb(const char *chunk, size_t chunk_len, void *ctx) {
    accumulator_t *acc = (accumulator_t *)ctx;
    acc->call_count++;
    TEST_ASSERT_TRUE_MESSAGE(acc->len + chunk_len < sizeof(acc->buf), "test accumulator overflow");
    memcpy(acc->buf + acc->len, chunk, chunk_len);
    acc->len += chunk_len;
    acc->buf[acc->len] = '\0';
    return ESP_OK;
}

static accumulator_t acc;

void setUp(void) {
    accumulator_reset(&acc);
}

void tearDown(void) {}

static sensor_reading_t make_reading(uint32_t epoch_sec, float lux, float temp_c,
                                      level_state_t level, uint8_t valid) {
    sensor_reading_t r;
    r.epoch_sec = epoch_sec;
    r.lux = lux;
    r.temp_c = temp_c;
    r.level = level;
    r.valid = valid;
    return r;
}

/* ---- reading_json_write_now ---- */

void test_write_now_serializes_single_valid_reading_shape(void) {
    sensor_reading_t r = make_reading(
        1700000000, 123.40f, 21.50f, LEVEL_MID,
        (uint8_t)(READING_VALID_LIGHT_BIT | READING_VALID_TEMP_BIT | READING_VALID_LEVEL_BIT |
                  READING_VALID_TIME_BIT));

    esp_err_t err = reading_json_write_now(&r, accumulate_cb, &acc);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"t\":1700000000"));
    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"time_valid\":true"));
    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"lux\":123.40"));
    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"temp_c\":21.50"));
    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"level\":\"MID\""));
    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"valid\":{"));
    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"light\":true"));
    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"temp\":true"));
    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"level\":true"));
    /* Must be a single well-formed object: exactly one opening/closing brace
     * pair at top level. */
    TEST_ASSERT_EQUAL_CHAR('{', acc.buf[0]);
    TEST_ASSERT_EQUAL_CHAR('}', acc.buf[acc.len - 1]);
}

void test_write_now_invalid_sensor_serializes_null_not_zero(void) {
    /* Light and temp both failed reads; level still valid. Per AC-HAPPY-2,
     * a cleared validity bit must serialize its value as JSON null, never
     * as a plausible-looking 0. */
    sensor_reading_t r = make_reading(1700000000, 0.0f, 0.0f, LEVEL_LOW, READING_VALID_LEVEL_BIT);

    esp_err_t err = reading_json_write_now(&r, accumulate_cb, &acc);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"lux\":null"));
    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"temp_c\":null"));
    TEST_ASSERT_NULL(strstr(acc.buf, "\"lux\":0"));
    TEST_ASSERT_NULL(strstr(acc.buf, "\"temp_c\":0"));
    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"light\":false"));
    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"temp\":false"));
    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"level\":\"LOW\""));
    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"time_valid\":false"));
}

/* ---- reading_json_write_history ---- */

void test_write_history_empty_input_serializes_well_formed_empty_arrays(void) {
    esp_err_t err = reading_json_write_history(NULL, 0, accumulate_cb, &acc);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_EQUAL_STRING(
        "{\"t\":[],\"time_valid\":[],\"lux\":[],\"temp_c\":[],\"level\":[]}", acc.buf);
}

void test_write_history_mixed_valid_invalid_time_serializes_per_entry(void) {
    sensor_reading_t readings[3] = {
        make_reading(10, 1.0f, 1.0f, LEVEL_FULL,
                     (uint8_t)(READING_VALID_LIGHT_BIT | READING_VALID_TIME_BIT)),
        make_reading(20, 2.0f, 2.0f, LEVEL_FULL, READING_VALID_LIGHT_BIT),
        make_reading(30, 3.0f, 3.0f, LEVEL_FULL,
                     (uint8_t)(READING_VALID_LIGHT_BIT | READING_VALID_TIME_BIT)),
    };

    esp_err_t err = reading_json_write_history(readings, 3, accumulate_cb, &acc);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"time_valid\":[true,false,true]"));
    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"t\":[10,20,30]"));
}

void test_write_history_parallel_arrays_equal_length_and_index_aligned(void) {
    sensor_reading_t readings[3] = {
        make_reading(1, 10.00f, 20.00f, LEVEL_FULL,
                     (uint8_t)(READING_VALID_LIGHT_BIT | READING_VALID_TEMP_BIT)),
        make_reading(2, 0.0f, 21.00f, LEVEL_MID, READING_VALID_TEMP_BIT),
        make_reading(3, 12.00f, 0.0f, LEVEL_LOW, READING_VALID_LIGHT_BIT),
    };

    esp_err_t err = reading_json_write_history(readings, 3, accumulate_cb, &acc);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"lux\":[10.00,null,12.00]"));
    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"temp_c\":[20.00,21.00,null]"));
    TEST_ASSERT_NOT_NULL(strstr(acc.buf, "\"level\":[\"FULL\",\"MID\",\"LOW\"]"));
}

void test_chunk_boundary_never_corrupts_concatenated_json(void) {
    /* A larger history payload forces many callback invocations (one per
     * field per entry, at minimum). Reassembling every fragment in order
     * must still produce byte-for-byte valid, exactly-expected JSON — proof
     * that no fragment boundary loses, duplicates, or reorders a byte. */
    sensor_reading_t readings[5];
    for (uint16_t i = 0; i < 5; i++) {
        readings[i] = make_reading((uint32_t)(1000 + i), (float)i, (float)(i * 2), LEVEL_FULL,
                                    (uint8_t)(READING_VALID_LIGHT_BIT | READING_VALID_TEMP_BIT |
                                              READING_VALID_LEVEL_BIT | READING_VALID_TIME_BIT));
    }

    esp_err_t err = reading_json_write_history(readings, 5, accumulate_cb, &acc);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Proves this test actually exercises chunking rather than one giant
     * write — the module is documented to call back per-field/per-element. */
    TEST_ASSERT_TRUE_MESSAGE(acc.call_count > 5, "expected many small callback fragments");

    TEST_ASSERT_EQUAL_STRING(
        "{\"t\":[1000,1001,1002,1003,1004],"
        "\"time_valid\":[true,true,true,true,true],"
        "\"lux\":[0.00,1.00,2.00,3.00,4.00],"
        "\"temp_c\":[0.00,2.00,4.00,6.00,8.00],"
        "\"level\":[\"FULL\",\"FULL\",\"FULL\",\"FULL\",\"FULL\"]}",
        acc.buf);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_write_now_serializes_single_valid_reading_shape);
    RUN_TEST(test_write_now_invalid_sensor_serializes_null_not_zero);
    RUN_TEST(test_write_history_empty_input_serializes_well_formed_empty_arrays);
    RUN_TEST(test_write_history_mixed_valid_invalid_time_serializes_per_entry);
    RUN_TEST(test_write_history_parallel_arrays_equal_length_and_index_aligned);
    RUN_TEST(test_chunk_boundary_never_corrupts_concatenated_json);
    return UNITY_END();
}
