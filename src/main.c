/**
 * app_main — Phase 2 boot-time one-shot sensor read.
 *
 * Reads each of the three sensors exactly once at boot and prints the results
 * over serial, then returns. This is the hardware exit criterion Phase 2's own
 * roadmap entry names ("all three sensors read on hardware via a one-shot read
 * at boot"), and it is deliberately the *whole* of the device wiring for now:
 * the 30-second sampler task, the ring store, and the mutex discipline all land
 * in Phase 3, and `app_main()` only ever wires and returns.
 *
 * Beyond proving the sensors on the bench, this file is what puts the Phase 2
 * driver modules into the firmware image at all. Until something in `src/`
 * referenced them, `lib/bh1750`, `lib/ds18b20_probe`, `lib/device_status` and
 * `lib/level_switches` were archived and then dropped by the linker — and
 * `level_switches` was never cross-compiled for the target at all, since only
 * the host `[env:native]` build touched it. Every module the phase added is
 * therefore called from here on purpose.
 *
 * Pins and switch polarity come from Kconfig (menu "Hydroponic Monitor"), never
 * from literals — float-switch polarity in particular cannot be assumed and is
 * a bench-determined value.
 */
#include <inttypes.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include "bh1750.h"
#include "device_status.h"
#include "ds18b20_probe.h"
#include "level_switches.h"
#include "reading_store.h"
#include "sampler.h"

static const char *TAG = "main";

/* Spacing between the debounce samples taken during the boot read. The state
 * machine needs LEVEL_SWITCHES_DEBOUNCE_N consecutive agreeing samples before
 * it commits anything, so a single read would only ever report UNKNOWN. Short
 * enough that boot is not visibly delayed; long enough that a chattering float
 * is not sampled three times inside one contact bounce. */
#define BOOT_LEVEL_SAMPLE_INTERVAL_MS 50

/* Kconfig bools are defined-or-undefined rather than 1/0, so normalize them to
 * real booleans before handing them to level_switches_init(). */
#ifdef CONFIG_HYDRO_LEVEL_INVERT_HIGH
#define HYDRO_LEVEL_INVERT_HIGH true
#else
#define HYDRO_LEVEL_INVERT_HIGH false
#endif
#ifdef CONFIG_HYDRO_LEVEL_INVERT_LOW
#define HYDRO_LEVEL_INVERT_LOW true
#else
#define HYDRO_LEVEL_INVERT_LOW false
#endif

static const char *level_state_name(level_switch_state_t state) {
    switch (state) {
        case LEVEL_SWITCH_STATE_FULL:
            return "FULL";
        case LEVEL_SWITCH_STATE_MID:
            return "MID";
        case LEVEL_SWITCH_STATE_LOW:
            return "LOW";
        case LEVEL_SWITCH_STATE_FAULT:
            return "FAULT";
        case LEVEL_SWITCH_STATE_UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

/* Creates the shared I2C master bus. Owned here (not by the BH1750 driver) so
 * later phases can hang additional I2C peripherals off the same bus. */
static esp_err_t i2c_bus_create(i2c_master_bus_handle_t *bus_out) {
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CONFIG_HYDRO_I2C_SDA_GPIO,
        .scl_io_num = CONFIG_HYDRO_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&bus_config, bus_out);
}

/* Both float switches: input, internal pull-up, no interrupts. The switch's
 * other leg is grounded, so a closed contact reads LOW. */
static esp_err_t level_gpio_configure(void) {
    gpio_config_t io_config = {
        .pin_bit_mask = (1ULL << CONFIG_HYDRO_LEVEL_HIGH_GPIO) |
                        (1ULL << CONFIG_HYDRO_LEVEL_LOW_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io_config);
}

/* Reads ambient light once. Leaves *lux_out untouched on failure — a failed
 * read is never a 0.0 reading. */
static esp_err_t read_light_once(float *lux_out) {
    i2c_master_bus_handle_t bus_handle = NULL;
    esp_err_t err = i2c_bus_create(&bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus create failed on SDA %d / SCL %d: %s",
                 CONFIG_HYDRO_I2C_SDA_GPIO, CONFIG_HYDRO_I2C_SCL_GPIO, esp_err_to_name(err));
        return err;
    }

    bh1750_t light_sensor;
    err = bh1750_init(&light_sensor, bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BH1750 init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = bh1750_read_lux(&light_sensor, lux_out);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BH1750 read failed: %s", esp_err_to_name(err));
    }
    return err;
}

/* Reads water temperature once. Leaves *temp_c_out untouched on failure. */
static esp_err_t read_temp_once(float *temp_c_out) {
    ds18b20_probe_t probe;
    esp_err_t err = ds18b20_probe_init(&probe, CONFIG_HYDRO_DS18B20_GPIO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DS18B20 init failed on GPIO %d: %s", CONFIG_HYDRO_DS18B20_GPIO,
                 esp_err_to_name(err));
        return err;
    }

    err = ds18b20_probe_read_temp_c(&probe, temp_c_out);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DS18B20 read failed: %s", esp_err_to_name(err));
    }
    return err;
}

/* Samples both float switches LEVEL_SWITCHES_DEBOUNCE_N times so the state
 * machine can commit out of UNKNOWN, and reports the committed band. */
static esp_err_t read_level_once(level_switch_state_t *state_out) {
    esp_err_t err = level_gpio_configure();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "level GPIO config failed (high %d, low %d): %s",
                 CONFIG_HYDRO_LEVEL_HIGH_GPIO, CONFIG_HYDRO_LEVEL_LOW_GPIO,
                 esp_err_to_name(err));
        return err;
    }

    level_switches_t level;
    level_switches_init(&level, HYDRO_LEVEL_INVERT_HIGH, HYDRO_LEVEL_INVERT_LOW);

    level_switch_state_t state = LEVEL_SWITCH_STATE_UNKNOWN;
    for (int i = 0; i < LEVEL_SWITCHES_DEBOUNCE_N; i++) {
        bool high_raw = gpio_get_level(CONFIG_HYDRO_LEVEL_HIGH_GPIO) != 0;
        bool low_raw = gpio_get_level(CONFIG_HYDRO_LEVEL_LOW_GPIO) != 0;
        state = level_switches_update(&level, high_raw, low_raw);
        ESP_LOGD(TAG, "level sample %d: high_raw=%d low_raw=%d -> %s", i + 1, (int)high_raw,
                 (int)low_raw, level_state_name(state));
        vTaskDelay(pdMS_TO_TICKS(BOOT_LEVEL_SAMPLE_INTERVAL_MS));
    }

    *state_out = state;
    return ESP_OK;
}

void app_main(void) {
    ESP_LOGI(TAG, "Hydroponic Monitor boot — Phase 2 one-shot sensor read");

    /* DRAM baseline for the Phase 5 re-check, an outstanding item carried over
     * from Phase 1 (which could not log it: app_main() was still empty). */
    ESP_LOGI(TAG, "free heap at boot: %" PRIu32 " bytes", esp_get_free_heap_size());

    bool sensor_fault = false;

    float lux = 0.0f;
    if (read_light_once(&lux) == ESP_OK) {
        ESP_LOGI(TAG, "ambient light: %.1f lux", lux);
    } else {
        sensor_fault = true;
    }

    float temp_c = 0.0f;
    if (read_temp_once(&temp_c) == ESP_OK) {
        ESP_LOGI(TAG, "water temperature: %.2f C", temp_c);
    } else {
        sensor_fault = true;
    }

    level_switch_state_t level_state = LEVEL_SWITCH_STATE_UNKNOWN;
    if (read_level_once(&level_state) == ESP_OK) {
        ESP_LOGI(TAG, "water level: %s (high GPIO %d, low GPIO %d, invert %d/%d)",
                 level_state_name(level_state), CONFIG_HYDRO_LEVEL_HIGH_GPIO,
                 CONFIG_HYDRO_LEVEL_LOW_GPIO, (int)HYDRO_LEVEL_INVERT_HIGH,
                 (int)HYDRO_LEVEL_INVERT_LOW);
    } else {
        sensor_fault = true;
    }

    /* LEVEL_FAULT is the more specific and more urgent condition, so it wins
     * when both are true — a stuck float is a physical wiring problem. */
    if (level_state == LEVEL_SWITCH_STATE_FAULT) {
        status_set(DEVICE_STATUS_LEVEL_FAULT);
    } else if (sensor_fault) {
        status_set(DEVICE_STATUS_SENSOR_FAULT);
    } else {
        status_set(DEVICE_STATUS_OK);
    }

    ESP_LOGI(TAG, "boot read complete");

    /* Phase 3: start the continuous 30 s sampler + the ring store it feeds.
     * reading_store_init() must run before sampler_start(), since the first
     * sample cycle can complete almost immediately and a push before init
     * would be silently dropped (see reading_store.c). app_main() stays
     * thin — all task/driver wiring lives in sampler.c. */
    reading_store_init();
    sampler_start();
    ESP_LOGI(TAG, "sampler started (interval: %d s)", CONFIG_HYDRO_SAMPLE_INTERVAL_SEC);
}
