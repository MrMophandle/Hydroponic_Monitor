/**
 * esp_shim.h — host-side stand-in for the small slice of ESP-IDF types/macros
 * that pure logic modules (e.g. lib/reading_store_core) need in order to
 * compile in the [env:native] PlatformIO environment, which has no ESP-IDF
 * framework and no FreeRTOS available.
 *
 * Only what is actually needed by host-testable modules lives here. It is
 * NOT a general ESP-IDF replacement — no FreeRTOS types, no driver headers.
 */
#ifndef HYDROPONIC_MONITOR_ESP_SHIM_H
#define HYDROPONIC_MONITOR_ESP_SHIM_H

#include <stdarg.h>

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NOT_FOUND 0x105

/* No-op structured logging macros: on-device this expands to ESP-IDF's
 * ESP_LOGI/W/E with a per-module TAG; on the host it compiles away to
 * nothing so no <esp_log.h> is required. */
#define ESP_LOGI(tag, format, ...) ((void)0)
#define ESP_LOGW(tag, format, ...) ((void)0)
#define ESP_LOGE(tag, format, ...) ((void)0)

#endif /* HYDROPONIC_MONITOR_ESP_SHIM_H */
