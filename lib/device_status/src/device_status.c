#include "device_status.h"

#include "esp_log.h"

static const char *TAG = "device_status";

void status_set(device_status_t status) {
    switch (status) {
        case DEVICE_STATUS_OK:
            ESP_LOGI(TAG, "status: OK");
            break;
        case DEVICE_STATUS_WIFI_DOWN:
            ESP_LOGW(TAG, "status: WIFI_DOWN");
            break;
        case DEVICE_STATUS_SENSOR_FAULT:
            ESP_LOGE(TAG, "status: SENSOR_FAULT");
            break;
        case DEVICE_STATUS_LEVEL_FAULT:
            ESP_LOGE(TAG, "status: LEVEL_FAULT");
            break;
        default:
            ESP_LOGW(TAG, "status: UNKNOWN (%d)", (int)status);
            break;
    }
}
