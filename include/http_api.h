/**
 * http_api — device-only `esp_http_server` wrapper exposing `/`, `/style.css`,
 * `/app.js`, `/dashboard-logic.js`, `/api/now`, and `/api/history`.
 *
 * Owns NO sensor peripheral (systemPatterns.md § Guiding Principles → One
 * Owner Per Peripheral): every reading served here comes from
 * `reading_store_downsample()` (`lib/reading_store/`) — the same read seam
 * any future consumer must use — never from a driver or `sensor_hub` call.
 *
 * Snapshot-then-stream (AC-HAPPY-3 / AC-ERROR-5): `reading_store_downsample()`
 * already does the bounded, lock-timeout-protected downsample and releases
 * the store mutex before returning to this module, so no handler here ever
 * performs network I/O while a store lock is held. A lock-acquire timeout
 * (`ESP_ERR_TIMEOUT`) is translated to HTTP 503, never a hang.
 *
 * Serialization is delegated entirely to `lib/reading_json/` — this module
 * owns only the `httpd_*` plumbing (registering handlers, reading the
 * `points` query parameter, translating store errors to HTTP statuses,
 * chunked-sending); see reading_json.h for the pure serializer and its
 * documented cross-half contract, which names this file as the responsible
 * device-only caller.
 *
 * `/` now serves the real embedded dashboard (Phase 6): `index.html`,
 * `style.css`, `app.js`, and `dashboard-logic.js` are embedded into the
 * firmware image via the `embed_web_assets.py` extra_script (platformio.ini
 * — see src/CMakeLists.txt for why this replaces both of PlatformIO's
 * documented embed mechanisms) and served verbatim from flash — no
 * template rendering, no dynamic HTML generation.
 *
 * Not host-testable: esp_http_server request routing requires the real
 * framework and is out of scope for `[env:native]` per the Test Strategy;
 * verified manually with `curl`/browser at the bench. The dashboard's pure
 * logic (src/web/dashboard-logic.js) IS host-tested — see
 * test/web/dashboard-logic.test.mjs — as the browser-side instance of this
 * project's Pure-Logic/Device-Only Split.
 */
#ifndef HYDROPONIC_MONITOR_HTTP_API_H
#define HYDROPONIC_MONITOR_HTTP_API_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Starts the HTTP server and registers `/`, `/style.css`, `/app.js`,
 * `/dashboard-logic.js`, `/api/now`, and `/api/history`.
 * Call once from app_main(), after wifi_conn_start() (the dashboard is
 * reachable once Wi-Fi associates; the server itself does not require an
 * active connection to start listening).
 *
 * @return ESP_OK once the server is listening and all handlers are
 *         registered; the first non-OK esp_err_t from httpd_start() or
 *         httpd_register_uri_handler() otherwise. A non-OK return means the
 *         dashboard is entirely unreachable and must be surfaced, not
 *         logged as if it succeeded.
 */
esp_err_t http_api_start(void);

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_HTTP_API_H */
