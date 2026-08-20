/**
 * sampler — see include/sampler.h for the module-level design rationale.
 *
 * Device-only. Sole owner of every sensor peripheral in the firmware (I2C
 * master bus, 1-Wire RMT bus, level-switch GPIO) and supplier of the real
 * bodies of sensor_hub's three read seams
 * (sensor_hub_light_read/temp_read/level_read — declared in
 * lib/sensor_hub/include/sensor_hub.h, called from
 * lib/sensor_hub/src/sensor_hub.c, defined here). This file is never
 * compiled for [env:native] (test_build_src = no excludes all of src/), so
 * unlike sensor_hub.c itself it is free to include the real ESP-IDF driver
 * headers directly.
 *
 * Peripherals are acquired exactly once, in sampler_sensors_init(), which
 * app_main() calls before its boot read — the boot read then reuses these
 * drivers through the seams rather than creating its own. See
 * include/sampler.h for the ownership contract and why a second acquire is a
 * hard failure rather than a no-op.
 */
#include "sampler.h"

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

#include "bh1750.h"
#include "ds18b20_probe.h"
#include "level_switches.h"
#include "reading_store.h"
#include "sensor_hub.h"

static const char *TAG = "sampler";

/* Sampler task sizing. Deliberately plain #defines rather than Kconfig —
 * these are internal implementation details of the task itself, not
 * bench-tunable hardware/timing parameters, mirroring the existing
 * precedent set by LEVEL_SWITCHES_DEBOUNCE_N and
 * BOOT_LEVEL_SAMPLE_INTERVAL_MS (both plain #defines in the Phase 1/2
 * modules). 4 KB comfortably covers the ~500 ms of driver calls plus
 * logging per cycle; priority 5 sits above the ESP-IDF main task's default
 * priority (1) but is not otherwise latency-critical in v1. */
#define SAMPLER_TASK_STACK_SIZE 4096
#define SAMPLER_TASK_PRIORITY 5

/* Spacing between debounce samples taken during each cycle's level read —
 * same rationale and value as src/main.c's boot-time
 * BOOT_LEVEL_SAMPLE_INTERVAL_MS. */
#define SAMPLER_LEVEL_SAMPLE_INTERVAL_MS 50

/* Kconfig bools are defined-or-undefined rather than 1/0 — normalize before
 * handing them to level_switches_init(), same as src/main.c. */
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

static i2c_master_bus_handle_t s_i2c_bus;
static bh1750_t s_light_sensor;
static bool s_light_ready = false;

static ds18b20_probe_t s_temp_probe;
static bool s_temp_ready = false;

static level_switches_t s_level_sw;
static bool s_level_ready = false;

/* Latches on the first sampler_sensors_init() call so a second one is
 * rejected as the caller bug it is, rather than re-acquiring peripherals. */
static bool s_sensors_initialized = false;

/** Creates the shared I2C master bus. Mirrors src/main.c's boot-read helper. */
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

/** Both float switches: input, internal pull-up, no interrupts. Mirrors src/main.c. */
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

/**
 * Initializes all three sensor drivers once, before the sampling loop
 * starts. A failure here is logged and leaves the corresponding `_ready`
 * flag false, so that sensor's read seam (below) fails cleanly every cycle
 * — driving straight into sensor_hub's consecutive-failure/offline
 * escalation — instead of dereferencing a never-initialized driver handle.
 *
 * This is the ONLY place in the firmware that acquires these peripherals
 * (see the ownership contract in include/sampler.h). It used to race
 * src/main.c's boot read, which stood up its own I2C and 1-Wire buses on the
 * same port/GPIO: the duplicate I2C acquire returned ESP_ERR_INVALID_STATE
 * and left the BH1750 permanently un-ready, and the duplicate 1-Wire acquire
 * silently orphaned an RMT TX+RX channel pair. The boot read now goes
 * through the sensor_hub read seams below instead of creating anything.
 */
static esp_err_t sensor_drivers_init(void) {
    esp_err_t first_err = ESP_OK;

    esp_err_t err = i2c_bus_create(&s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus create failed: %s", esp_err_to_name(err));
        first_err = err;
    } else {
        err = bh1750_init(&s_light_sensor, s_i2c_bus);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "BH1750 init failed: %s", esp_err_to_name(err));
            if (first_err == ESP_OK) {
                first_err = err;
            }
        } else {
            s_light_ready = true;
        }
    }

    err = ds18b20_probe_init(&s_temp_probe, CONFIG_HYDRO_DS18B20_GPIO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DS18B20 init failed: %s", esp_err_to_name(err));
        if (first_err == ESP_OK) {
            first_err = err;
        }
    } else {
        s_temp_ready = true;
    }

    err = level_gpio_configure();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "level GPIO config failed: %s", esp_err_to_name(err));
        if (first_err == ESP_OK) {
            first_err = err;
        }
    } else {
        level_switches_init(&s_level_sw, HYDRO_LEVEL_INVERT_HIGH, HYDRO_LEVEL_INVERT_LOW);
        s_level_ready = true;
    }

    return first_err;
}

esp_err_t sampler_sensors_init(void) {
    /* Guard against a second call: these are one-shot peripheral acquires,
     * so a repeat would fail rather than re-init, and the resulting
     * ESP_ERR_INVALID_STATE would be reported as a sensor fault when the
     * real defect is a double-wiring bug in the caller. */
    if (s_sensors_initialized) {
        ESP_LOGE(TAG, "sampler_sensors_init() called twice — peripherals are already owned");
        return ESP_ERR_INVALID_STATE;
    }
    s_sensors_initialized = true;

    esp_err_t err = sensor_drivers_init();
    if (err != ESP_OK) {
        /* Collapse to ESP_FAIL per the documented contract: the specific
         * per-driver errors are already logged above, and the caller's only
         * decision is "all good" vs "degraded, keep going". */
        ESP_LOGW(TAG, "sensor init degraded (light=%d temp=%d level=%d)", (int)s_light_ready,
                 (int)s_temp_ready, (int)s_level_ready);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "all three sensor drivers initialized");
    return ESP_OK;
}

/* ---- sensor_hub's three read seams (lib/sensor_hub/include/sensor_hub.h) — real device bodies ---- */

esp_err_t sensor_hub_light_read(float *lux_out) {
    if (!s_light_ready) {
        return ESP_FAIL;
    }
    return bh1750_read_lux(&s_light_sensor, lux_out);
}

esp_err_t sensor_hub_temp_read(float *temp_c_out) {
    if (!s_temp_ready) {
        return ESP_FAIL;
    }
    return ds18b20_probe_read_temp_c(&s_temp_probe, temp_c_out);
}

esp_err_t sensor_hub_level_read(level_switch_state_t *level_out) {
    if (!s_level_ready) {
        return ESP_FAIL;
    }
    level_switch_state_t state = LEVEL_SWITCH_STATE_UNKNOWN;
    for (int i = 0; i < LEVEL_SWITCHES_DEBOUNCE_N; i++) {
        bool high_raw = gpio_get_level(CONFIG_HYDRO_LEVEL_HIGH_GPIO) != 0;
        bool low_raw = gpio_get_level(CONFIG_HYDRO_LEVEL_LOW_GPIO) != 0;
        state = level_switches_update(&s_level_sw, high_raw, low_raw);
        vTaskDelay(pdMS_TO_TICKS(SAMPLER_LEVEL_SAMPLE_INTERVAL_MS));
    }
    *level_out = state;
    return ESP_OK;
}

/** Maps sensor_hub/level_switches's level_switch_state_t (Phase 2) onto
 * reading_store_core's level_state_t (Phase 1) — two separately-defined
 * five-valued enums with the same meaning, a Phase 1 design quirk (see
 * reading_store_core.h) that this is the first phase to actually bridge. */
static level_state_t to_store_level_state(level_switch_state_t state) {
    switch (state) {
        case LEVEL_SWITCH_STATE_FULL:
            return LEVEL_FULL;
        case LEVEL_SWITCH_STATE_MID:
            return LEVEL_MID;
        case LEVEL_SWITCH_STATE_LOW:
            return LEVEL_LOW;
        case LEVEL_SWITCH_STATE_FAULT:
            return LEVEL_FAULT;
        case LEVEL_SWITCH_STATE_UNKNOWN:
        default:
            return LEVEL_UNKNOWN;
    }
}

static void sampler_task(void *arg) {
    (void)arg;

    sensor_hub_t hub;
    sensor_hub_init(&hub);

    /* Absolute-deadline pacing (systemPatterns.md § Guiding Principles →
     * Periodic Work Uses Absolute Deadlines). The previous vTaskDelay() slept
     * a fixed interval AFTER the work, making the true period
     * interval + read-window (~31.1 s for a nominal 30 s setting). That
     * drifted the ring's real span past the documented 24 h AND broke
     * reading_store_core_downsample()'s precondition, which selects samples
     * by index and is only evenly spaced in TIME if this period is constant.
     * xTaskDelayUntil() holds the period constant regardless of how long the
     * reads took. */
    TickType_t next_wake = xTaskGetTickCount();
    const TickType_t period_ticks =
        pdMS_TO_TICKS((uint32_t)CONFIG_HYDRO_SAMPLE_INTERVAL_SEC * 1000U);

    for (;;) {
        /* TWDT subscription brackets ONLY the read window (~1.1 s nominal
         * across the three sensors, up to ~3.3 s when both I2C transactions
         * hit their 1000 ms timeouts), never the
         * CONFIG_HYDRO_SAMPLE_INTERVAL_SEC sleep below. A permanently-
         * subscribed task would blow the watchdog on its very first sleep —
         * see AC-ASYNC-2 in the task file for the full rationale.
         *
         * Note what this actually buys: CONFIG_ESP_TASK_WDT_TIMEOUT_S is 5 s
         * and CONFIG_ESP_TASK_WDT_PANIC is NOT set, so a trip prints a
         * warning plus backtrace and execution continues. It is a detector,
         * not a recovery mechanism. Set CONFIG_ESP_TASK_WDT_PANIC=y if
         * reboot-on-hang is wanted. */
        bool wdt_subscribed = false;
        esp_err_t wdt_err = esp_task_wdt_add(NULL);
        if (wdt_err == ESP_OK) {
            wdt_subscribed = true;
        } else {
            /* Non-fatal: sampling is still correct without the watchdog, we
             * just lose hang detection for this cycle. */
            ESP_LOGW(TAG, "TWDT subscribe failed: %s", esp_err_to_name(wdt_err));
        }

        sensor_hub_reading_t hub_reading = sensor_hub_run_cycle(&hub);

        if (wdt_subscribed) {
            wdt_err = esp_task_wdt_delete(NULL);
            if (wdt_err != ESP_OK) {
                /* Would leave the task subscribed across the sleep and trip
                 * the watchdog every cycle, so this one is worth ESP_LOGE. */
                ESP_LOGE(TAG, "TWDT unsubscribe failed: %s", esp_err_to_name(wdt_err));
            }
        }

        sensor_reading_t reading = {
            .uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000LL),
            .lux = hub_reading.lux,
            .temp_c = hub_reading.temp_c,
            .level = to_store_level_state(hub_reading.level),
            .valid = (uint8_t)((hub_reading.light_valid ? 0x01 : 0) |
                                (hub_reading.temp_valid ? 0x02 : 0) |
                                (hub_reading.level_valid ? 0x04 : 0)),
        };
        /* Pushed unconditionally, even on a total-failure cycle — a failed
         * sensor's sample still gets a slot in the ring with its valid bit
         * clear, per Decision 5 (failed reads are stored as invalid, never
         * as 0), so the chart draws a gap rather than silently skipping a
         * whole 30-second tick. */
        reading_store_push(&reading);

        /* pdFALSE means the deadline had already passed — the read window
         * overran the whole sample interval, so this returns immediately
         * instead of sleeping. Sampling stays correct (no lag accumulates),
         * but the even-spacing precondition is violated for that cycle, so
         * say so rather than silently drifting. */
        if (xTaskDelayUntil(&next_wake, period_ticks) == pdFALSE) {
            ESP_LOGW(TAG, "sample cycle overran the %d s interval — sample spacing is uneven",
                     CONFIG_HYDRO_SAMPLE_INTERVAL_SEC);
        }
    }
}

esp_err_t sampler_start(void) {
    BaseType_t created = xTaskCreatePinnedToCore(sampler_task, "sampler", SAMPLER_TASK_STACK_SIZE,
                                                 NULL, SAMPLER_TASK_PRIORITY, NULL, tskNO_AFFINITY);
    if (created != pdPASS) {
        /* Nothing will ever be sampled if this happens, so it must not be
         * swallowed — the previous void signature let boot log "sampler
         * started" over a task that did not exist. */
        ESP_LOGE(TAG, "failed to create sampler task (stack %d, prio %d) — no sampling will occur",
                 SAMPLER_TASK_STACK_SIZE, SAMPLER_TASK_PRIORITY);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
