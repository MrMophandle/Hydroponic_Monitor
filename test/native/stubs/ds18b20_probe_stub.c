/**
 * ds18b20_probe_stub — native-only, test-controllable substitute for
 * sensor_hub's temperature-read seam (sensor_hub_temp_read()). See
 * sensor_hub_stubs.h.
 */
#include "sensor_hub_stubs.h"

esp_err_t ds18b20_probe_stub_next_result = ESP_OK;
float ds18b20_probe_stub_next_temp_c = 0.0f;

void ds18b20_probe_stub_reset(void) {
    ds18b20_probe_stub_next_result = ESP_OK;
    ds18b20_probe_stub_next_temp_c = 0.0f;
}

esp_err_t sensor_hub_temp_read(float *temp_c_out) {
    if (ds18b20_probe_stub_next_result == ESP_OK) {
        *temp_c_out = ds18b20_probe_stub_next_temp_c;
    }
    return ds18b20_probe_stub_next_result;
}
