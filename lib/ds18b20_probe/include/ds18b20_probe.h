/**
 * ds18b20_probe — 1-Wire water-temperature driver for the DROK waterproof
 * DS18B20 probe.
 *
 * Device-only: built on the managed `espressif/onewire_bus` +
 * `espressif/ds18b20` components (declared in src/idf_component.yml) which
 * talk to the real 1-Wire bus. There is no host unit test for this module;
 * 1-Wire timing/protocol is bench-verified on hardware, per
 * systemPatterns.md § Testing Patterns.
 *
 * Exactly one DS18B20 is expected on the bus (a single probe). Init fails
 * with ESP_ERR_NOT_FOUND if the bus scan finds none.
 */
#ifndef HYDROPONIC_MONITOR_DS18B20_PROBE_H
#define HYDROPONIC_MONITOR_DS18B20_PROBE_H

#include "ds18b20.h"
#include "esp_err.h"
#include "onewire_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Driver handle. Callers allocate this and pass it to every call. */
typedef struct {
    onewire_bus_handle_t bus_handle;
    ds18b20_device_handle_t device_handle;
} ds18b20_probe_t;

/**
 * Creates the 1-Wire bus on `gpio_num` (the pin the DROK probe's data line +
 * 4.7 kOhm pull-up are wired to), scans it, and binds to the first DS18B20
 * device found.
 *
 * Returns ESP_OK on success; ESP_ERR_NOT_FOUND if the bus scan finds no
 * device (broken wire, missing pull-up, wrong pin); or the underlying
 * onewire_bus error otherwise.
 */
esp_err_t ds18b20_probe_init(ds18b20_probe_t *probe, int gpio_num);

/**
 * Triggers a temperature conversion and reads it back. Blocks for the
 * conversion time (device-internal delay) — callers must not call this from
 * a context that cannot block.
 *
 * On success, writes the measured temperature in degrees Celsius to
 * *temp_c_out and returns ESP_OK. On failure, *temp_c_out is left
 * unmodified and the underlying error is returned — the caller (sensor_hub,
 * in a later phase) is responsible for treating that as a failed reading,
 * never a 0.0 reading.
 */
esp_err_t ds18b20_probe_read_temp_c(ds18b20_probe_t *probe, float *temp_c_out);

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_DS18B20_PROBE_H */
