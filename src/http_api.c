/**
 * http_api — see include/http_api.h for the module-level design rationale.
 */
#include "http_api.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "reading_json.h"
#include "reading_store.h"
#include "reading_store_core.h"

static const char *TAG = "http_api";

/* `points` query-parameter bounds for /api/history (AC-HAPPY-3 / the design
 * doc's snapshot-RAM rationale). Plain #defines rather than Kconfig: these
 * are internal API-contract constants, not a bench-tunable hardware value —
 * same precedent as SAMPLER_TASK_STACK_SIZE in src/sampler.c. Changing
 * POINTS_MAX changes the size of the static snapshot buffer below, so it is
 * fixed at compile time deliberately. */
#define POINTS_MAX 500
#define POINTS_DEFAULT 180

/* Embedded dashboard assets (Phase 6), via the `extra_scripts =
 * embed_web_assets.py` entry in platformio.ini (see src/CMakeLists.txt and
 * embed_web_assets.py's module docstring for why this hand-rolled script is
 * used instead of idf_component_register's EMBED_TXTFILES or PlatformIO's
 * own board_build.embed_txtfiles — both were tried and both failed to link
 * under this platform/version). Each file is linked into the firmware image
 * as a null-terminated byte array, under a symbol name derived from the
 * file's BASENAME (not its full path) with non-identifier characters
 * replaced by `_`. Null-termination is what makes HTTPD_RESP_USE_STRLEN
 * valid on these buffers below. */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t style_css_start[] asm("_binary_style_css_start");
extern const uint8_t style_css_end[] asm("_binary_style_css_end");
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[] asm("_binary_app_js_end");
extern const uint8_t dashboard_logic_js_start[] asm("_binary_dashboard_logic_js_start");
extern const uint8_t dashboard_logic_js_end[] asm("_binary_dashboard_logic_js_end");

/** Adapts reading_json's write callback to esp_http_server's chunked send.
 * `ctx` is the httpd_req_t* for the in-flight request. */
static esp_err_t http_chunk_write_cb(const char *chunk, size_t len, void *ctx) {
    httpd_req_t *req = (httpd_req_t *)ctx;
    esp_err_t err = httpd_resp_send_chunk(req, chunk, (ssize_t)len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_resp_send_chunk failed: %s", esp_err_to_name(err));
    }
    return err;
}

/** Sends a bare 503 with no body — used when the store lock could not be
 * acquired within its timeout (AC-ERROR-5). Never blocks. */
static esp_err_t send_503(httpd_req_t *req) {
    esp_err_t err = httpd_resp_set_status(req, "503 Service Unavailable");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_resp_set_status(503) failed: %s", esp_err_to_name(err));
        return err;
    }
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t root_get_handler(httpd_req_t *req) {
    esp_err_t err = httpd_resp_set_type(req, "text/html");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_resp_set_type failed: %s", esp_err_to_name(err));
        return err;
    }
    return httpd_resp_send(req, (const char *)index_html_start, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t style_css_get_handler(httpd_req_t *req) {
    esp_err_t err = httpd_resp_set_type(req, "text/css");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_resp_set_type failed: %s", esp_err_to_name(err));
        return err;
    }
    return httpd_resp_send(req, (const char *)style_css_start, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t app_js_get_handler(httpd_req_t *req) {
    esp_err_t err = httpd_resp_set_type(req, "application/javascript");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_resp_set_type failed: %s", esp_err_to_name(err));
        return err;
    }
    return httpd_resp_send(req, (const char *)app_js_start, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t dashboard_logic_js_get_handler(httpd_req_t *req) {
    esp_err_t err = httpd_resp_set_type(req, "application/javascript");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_resp_set_type failed: %s", esp_err_to_name(err));
        return err;
    }
    return httpd_resp_send(req, (const char *)dashboard_logic_js_start, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t now_get_handler(httpd_req_t *req) {
    sensor_reading_t reading;
    uint16_t written = 0;

    esp_err_t err = reading_store_downsample(&reading, 1, &written);
    if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "/api/now: store lock timed out — returning 503");
        return send_503(req);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "/api/now: reading_store_downsample failed: %s", esp_err_to_name(err));
        return err;
    }

    if (written == 0) {
        /* No sample has been recorded yet. Report a fully-invalid,
         * UNKNOWN-level reading rather than fabricating one — the same
         * "never a plausible-looking value" rule applies before the first
         * sample as it does to a single failed sensor. */
        memset(&reading, 0, sizeof(reading));
        reading.level = LEVEL_UNKNOWN;
    }

    err = httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_resp_set_type failed: %s", esp_err_to_name(err));
        return err;
    }

    err = reading_json_write_now(&reading, http_chunk_write_cb, req);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "/api/now: reading_json_write_now failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Zero-length chunk terminates a chunked response. */
    return httpd_resp_send_chunk(req, NULL, 0);
}

/** Parses and clamps the `points` query parameter into [1, POINTS_MAX].
 * Absent or non-numeric (unparseable) falls back to POINTS_DEFAULT;
 * zero/negative/too-large (numeric but out of range) clamp to the nearer
 * bound instead of the default, since a value was actually supplied. */
/* Fixed, stack-only query-string buffer — no request to this handler ever
 * legitimately needs more than this (a single "points=<digits>" parameter),
 * so a too-long query string is treated the same as an absent one rather
 * than allocated for. */
#define HTTP_API_QUERY_BUF_SIZE 64

static uint16_t parse_points_param(httpd_req_t *req) {
    size_t query_len = httpd_req_get_url_query_len(req);
    if (query_len == 0 || query_len >= HTTP_API_QUERY_BUF_SIZE) {
        return POINTS_DEFAULT;
    }

    char query[HTTP_API_QUERY_BUF_SIZE];
    esp_err_t err = httpd_req_get_url_query_str(req, query, sizeof(query));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "points param: query string read failed (%s) — using default",
                 esp_err_to_name(err));
        return POINTS_DEFAULT;
    }

    char value[16];
    err = httpd_query_key_value(query, "points", value, sizeof(value));
    if (err != ESP_OK) {
        /* Absent, or too long to fit `value` (which no valid points value
         * ever would) — both fall back to the default. */
        return POINTS_DEFAULT;
    }

    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0') {
        /* Non-numeric / unparseable. */
        return POINTS_DEFAULT;
    }

    if (parsed < 1) {
        return 1;
    }
    if (parsed > POINTS_MAX) {
        return POINTS_MAX;
    }
    return (uint16_t)parsed;
}

static esp_err_t history_get_handler(httpd_req_t *req) {
    uint16_t points = parse_points_param(req);

    /* Statically bounded, never heap-allocated — sized for the worst case
     * (POINTS_MAX) regardless of how many points are actually requested, so
     * this handler never grows the snapshot with ring fullness (AC-HAPPY-3).
     * ~500 * 20 B =~ 10 KB stack usage; the HTTP server task's stack must be
     * sized to comfortably cover this (default httpd task stack is 4 KB * a
     * config multiplier — verified against the device build's actual link,
     * not assumed, since a stack overflow here would be silent corruption
     * rather than a clean failure). */
    static sensor_reading_t snapshot[POINTS_MAX];
    uint16_t written = 0;

    esp_err_t err = reading_store_downsample(snapshot, points, &written);
    if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "/api/history: store lock timed out — returning 503");
        return send_503(req);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "/api/history: reading_store_downsample failed: %s", esp_err_to_name(err));
        return err;
    }

    err = httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_resp_set_type failed: %s", esp_err_to_name(err));
        return err;
    }

    err = reading_json_write_history(snapshot, written, http_chunk_write_cb, req);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "/api/history: reading_json_write_history failed: %s", esp_err_to_name(err));
        return err;
    }

    return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t http_api_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    static const httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    static const httpd_uri_t style_css_uri = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = style_css_get_handler,
    };
    static const httpd_uri_t app_js_uri = {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = app_js_get_handler,
    };
    static const httpd_uri_t dashboard_logic_js_uri = {
        .uri = "/dashboard-logic.js",
        .method = HTTP_GET,
        .handler = dashboard_logic_js_get_handler,
    };
    static const httpd_uri_t now_uri = {
        .uri = "/api/now",
        .method = HTTP_GET,
        .handler = now_get_handler,
    };
    static const httpd_uri_t history_uri = {
        .uri = "/api/history",
        .method = HTTP_GET,
        .handler = history_get_handler,
    };

    err = httpd_register_uri_handler(server, &root_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register / failed: %s", esp_err_to_name(err));
        return err;
    }
    err = httpd_register_uri_handler(server, &style_css_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register /style.css failed: %s", esp_err_to_name(err));
        return err;
    }
    err = httpd_register_uri_handler(server, &app_js_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register /app.js failed: %s", esp_err_to_name(err));
        return err;
    }
    err = httpd_register_uri_handler(server, &dashboard_logic_js_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register /dashboard-logic.js failed: %s", esp_err_to_name(err));
        return err;
    }
    err = httpd_register_uri_handler(server, &now_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register /api/now failed: %s", esp_err_to_name(err));
        return err;
    }
    err = httpd_register_uri_handler(server, &history_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register /api/history failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "HTTP API started: /, /style.css, /app.js, /dashboard-logic.js, "
                  "/api/now, /api/history");
    return ESP_OK;
}
