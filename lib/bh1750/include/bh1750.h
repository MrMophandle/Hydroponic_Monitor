/**
 * bh1750 — I2C ambient-light driver for the HiLetgo GY-302 / BH1750 module.
 *
 * Device-only: talks to the real I2C bus via the newer `i2c_master` API
 * (ESP-IDF 5.2+, provided by the espidf@6.9.0 platform pin — see
 * platformio.ini). There is no host unit test for this module; the I2C
 * transaction protocol itself is bench-verified on hardware, per
 * systemPatterns.md § Testing Patterns ("peripheral bus I/O itself ...
 * bench-verified on hardware, not host-unit-tested").
 *
 * Module address is fixed at 0x23 (ADDR pin tied low on the HiLetgo board —
 * confirmed hardware wiring, not a configurable value).
 */
#ifndef HYDROPONIC_MONITOR_BH1750_H
#define HYDROPONIC_MONITOR_BH1750_H

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** BH1750 I2C address with ADDR tied low (the HiLetgo GY-302 wiring). */
#define BH1750_I2C_ADDRESS 0x23

/** Driver handle. Callers allocate this and pass it to every call. */
typedef struct {
    i2c_master_dev_handle_t dev_handle;
} bh1750_t;

/**
 * Attaches the BH1750 as a device on an already-initialized I2C master bus
 * and powers it on. `bus_handle` is owned by the caller (created once at
 * startup and shared across I2C peripherals, if any are added later).
 *
 * Returns ESP_OK on success, or the underlying i2c_master error otherwise.
 */
esp_err_t bh1750_init(bh1750_t *dev, i2c_master_bus_handle_t bus_handle);

/**
 * Triggers a one-shot high-resolution (1 lx) measurement and reads it back.
 * Blocks for the sensor's ~180ms measurement window (vTaskDelay) — callers
 * must not call this from a context that cannot block.
 *
 * On success, writes the measured illuminance in lux to *lux_out and returns
 * ESP_OK. On I2C failure, *lux_out is left unmodified and the underlying
 * error is returned — the caller (sensor_hub, in a later phase) is
 * responsible for treating that as a failed reading, never a 0.0 reading.
 */
esp_err_t bh1750_read_lux(bh1750_t *dev, float *lux_out);

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_BH1750_H */
