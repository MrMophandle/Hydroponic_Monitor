/**
 * bh1750_stub — native-only, test-controllable substitute for sensor_hub's
 * light-read seam (sensor_hub_light_read()). See sensor_hub_stubs.h.
 */
#include "sensor_hub_stubs.h"

esp_err_t bh1750_stub_next_result = ESP_OK;
float bh1750_stub_next_lux = 0.0f;

void bh1750_stub_reset(void) {
    bh1750_stub_next_result = ESP_OK;
    bh1750_stub_next_lux = 0.0f;
}

esp_err_t sensor_hub_light_read(float *lux_out) {
    if (bh1750_stub_next_result == ESP_OK) {
        *lux_out = bh1750_stub_next_lux;
    }
    return bh1750_stub_next_result;
}
