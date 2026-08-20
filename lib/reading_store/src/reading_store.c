/**
 * reading_store — see lib/reading_store/include/reading_store.h for the
 * module-level design rationale (thin locking delegator to
 * reading_store_core).
 */
#include "reading_store.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"

static const char *TAG = "reading_store";

static reading_store_core_t s_store;
static SemaphoreHandle_t s_mutex = NULL;

void reading_store_init(void) {
    reading_store_core_init(&s_store);
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "failed to create store mutex");
    }
}

void reading_store_push(const sensor_reading_t *reading) {
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "push before reading_store_init(); dropping reading");
        return;
    }
    /* Indefinite wait: the writer is trusted to be fast (a ~20-byte copy),
     * and by this point the sensor reads that could actually block have
     * already happened outside the lock (see src/sampler.c). */
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    reading_store_core_push(&s_store, reading);
    xSemaphoreGive(s_mutex);
}

esp_err_t reading_store_downsample(sensor_reading_t *out, uint16_t points, uint16_t *written_out) {
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "downsample before reading_store_init()");
        return ESP_FAIL;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(READING_STORE_READER_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    uint16_t written = reading_store_core_downsample(&s_store, out, points);
    xSemaphoreGive(s_mutex);

    if (written_out != NULL) {
        *written_out = written;
    }
    return ESP_OK;
}
