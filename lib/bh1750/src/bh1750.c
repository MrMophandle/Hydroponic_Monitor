#include "bh1750.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

static const char *TAG = "bh1750";

/* BH1750 command opcodes (Rohm datasheet). */
#define BH1750_CMD_POWER_ON 0x01
#define BH1750_CMD_ONE_TIME_HIGH_RES_MODE 0x20

/* One-Time High-Resolution Mode: 1 lx resolution, up to ~180ms conversion
 * time per the datasheet's typical/max timing. Rounded up for margin. */
#define BH1750_MEASUREMENT_DELAY_MS 180

/* i2c_master transaction timeout; the bus is on-board and short, so a
 * generous fixed timeout is sufficient rather than a configurable value. */
#define BH1750_I2C_TIMEOUT_MS 1000

esp_err_t bh1750_init(bh1750_t *dev, i2c_master_bus_handle_t bus_handle) {
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BH1750_I2C_ADDRESS,
        .scl_speed_hz = 100000,
    };

    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_config, &dev->dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to add I2C device: %d", err);
        return err;
    }

    uint8_t power_on_cmd = BH1750_CMD_POWER_ON;
    err = i2c_master_transmit(dev->dev_handle, &power_on_cmd, 1, BH1750_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to power on: %d", err);
        return err;
    }

    ESP_LOGI(TAG, "initialized at address 0x%02x", BH1750_I2C_ADDRESS);
    return ESP_OK;
}

esp_err_t bh1750_read_lux(bh1750_t *dev, float *lux_out) {
    uint8_t measure_cmd = BH1750_CMD_ONE_TIME_HIGH_RES_MODE;
    esp_err_t err = i2c_master_transmit(dev->dev_handle, &measure_cmd, 1, BH1750_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to trigger measurement: %d", err);
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(BH1750_MEASUREMENT_DELAY_MS));

    uint8_t raw[2] = {0};
    err = i2c_master_receive(dev->dev_handle, raw, sizeof(raw), BH1750_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to read measurement: %d", err);
        return err;
    }

    uint16_t raw_value = ((uint16_t)raw[0] << 8) | raw[1];
    /* Datasheet: lux = raw / 1.2 in high-resolution mode. */
    *lux_out = (float)raw_value / 1.2f;
    return ESP_OK;
}
