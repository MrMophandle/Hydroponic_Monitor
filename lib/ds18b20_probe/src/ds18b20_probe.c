#include "ds18b20_probe.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "ds18b20_probe";

esp_err_t ds18b20_probe_init(ds18b20_probe_t *probe, int gpio_num) {
    onewire_bus_config_t bus_config = {
        .bus_gpio_num = gpio_num,
    };
    onewire_bus_rmt_config_t rmt_config = {
        /* 1 ROM command byte + 8 ROM number bytes + 1 device command byte,
         * matching the espressif/ds18b20 reference example's sizing. */
        .max_rx_bytes = 10,
    };

    esp_err_t err = onewire_new_bus_rmt(&bus_config, &rmt_config, &probe->bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to create 1-Wire bus on GPIO %d: %d", gpio_num, err);
        return err;
    }

    /* Exactly one DS18B20 is expected on this bus (a single probe), so bind
     * directly rather than enumerating + matching a family ID. */
    ds18b20_config_t ds_config;
    /* espressif/ds18b20 0.3.1's ds18b20_config_t is a reserved, currently-empty
     * struct — `= {0}` triggers "excess elements" since there are no members to
     * initialize. memset zero-fills defensively regardless of struct size, so
     * this stays correct (and warning-free) if a future component bump adds
     * fields to the struct. */
    memset(&ds_config, 0, sizeof(ds_config));
    err = ds18b20_new_device_from_bus(probe->bus_handle, &ds_config, &probe->device_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no DS18B20 device found on the 1-Wire bus (GPIO %d): %d", gpio_num, err);
        return err;
    }

    err = ds18b20_set_resolution(probe->device_handle, DS18B20_RESOLUTION_12B);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to set resolution: %d", err);
        return err;
    }

    ESP_LOGI(TAG, "initialized DS18B20 on GPIO %d", gpio_num);
    return ESP_OK;
}

esp_err_t ds18b20_probe_read_temp_c(ds18b20_probe_t *probe, float *temp_c_out) {
    esp_err_t err = ds18b20_trigger_temperature_conversion(probe->device_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to trigger conversion: %d", err);
        return err;
    }

    float temp_c = 0.0f;
    err = ds18b20_get_temperature(probe->device_handle, &temp_c);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to read temperature: %d", err);
        return err;
    }

    *temp_c_out = temp_c;
    return ESP_OK;
}
