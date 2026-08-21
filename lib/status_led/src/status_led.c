/**
 * status_led — see include/status_led.h for the module-level design
 * rationale. Device-only: creates exactly one RMT TX channel (AC-VERIFY-7)
 * for the onboard WS2812 and delegates all bit-level timing to the vendored
 * `led_strip_encoder` (Espressif's ESP-IDF RMT LED-strip example,
 * lib/status_led/{include,src}/led_strip_encoder.{h,c}, copied verbatim —
 * do not modify it here).
 *
 * mem_block_symbols is set to 48, the verified legal minimum on ESP32-S3
 * (esp_driver_rmt/src/rmt_tx.c requires an even value >=
 * SOC_RMT_MEM_WORDS_PER_CHANNEL, which is 48 on this SoC; rmt_tx.c rounds
 * mem_block_num up from mem_block_symbols, so the vendored example's 64
 * would consume 2 of the SoC's 4 RMT memory blocks for a single LED, while
 * 48 consumes exactly 1 — see memory-bank/creative/onboard-status-led-design.md).
 * That leaves 3 of 4 RMT memory blocks for the DS18B20 1-Wire bus's TX+RX
 * pair (sampler.c) plus headroom, instead of 2.
 */
#include "status_led.h"

#include "freertos/FreeRTOS.h"

#include "driver/rmt_tx.h"
#include "esp_log.h"

#include "led_strip_encoder.h"

static const char *TAG = "status_led";

/* WS2812 timing resolution: 10 MHz, 1 tick = 0.1 us — matches the vendored
 * encoder's expected T0H/T0L/T1H/T1L timing (see led_strip_encoder.c). */
#define STATUS_LED_RMT_RESOLUTION_HZ 10000000

/* Legal minimum RMT memory block size on ESP32-S3 (SOC_RMT_MEM_WORDS_PER_CHANNEL).
 * See the module doc-comment above for why this is 48, not the vendored
 * example's 64. */
#define STATUS_LED_RMT_MEM_BLOCK_SYMBOLS 48

/* One WS2812 pixel: 3 bytes, wire order GRB (not RGB — see status_led_show()). */
#define STATUS_LED_FRAME_BYTES 3

static rmt_channel_handle_t s_led_chan = NULL;
static rmt_encoder_handle_t s_led_encoder = NULL;

/**
 * Resolves the Kconfig `choice` (CONFIG_HYDRO_STATUS_LED_GPIO_48/_47/_38,
 * see src/Kconfig.projbuild) to a literal gpio_num_t. This if/else ladder is
 * the ONE place project convention allows resolving a Kconfig choice to a
 * numeric pin literal (AC-VERIFY-6 requires the choice shape specifically so
 * this resolution is visible and greppable here, not buried in a plain
 * int+range default that looks like an ordinary pin assignment).
 */
static gpio_num_t status_led_configured_gpio(void) {
#if defined(CONFIG_HYDRO_STATUS_LED_GPIO_47)
    return GPIO_NUM_47;
#elif defined(CONFIG_HYDRO_STATUS_LED_GPIO_38)
    return GPIO_NUM_38;
#else
    /* CONFIG_HYDRO_STATUS_LED_GPIO_48 — the Kconfig default and the vendor
     * Q&A answer for this board's onboard RGB LED. */
    return GPIO_NUM_48;
#endif
}

esp_err_t status_led_init(void) {
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = status_led_configured_gpio(),
        .mem_block_symbols = STATUS_LED_RMT_MEM_BLOCK_SYMBOLS,
        .resolution_hz = STATUS_LED_RMT_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    esp_err_t err = rmt_new_tx_channel(&tx_chan_config, &s_led_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    led_strip_encoder_config_t encoder_config = {
        .resolution = STATUS_LED_RMT_RESOLUTION_HZ,
    };
    err = rmt_new_led_strip_encoder(&encoder_config, &s_led_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_led_strip_encoder failed: %s", esp_err_to_name(err));
        /* Release the channel acquired above — status_led_init() is called
         * exactly once at boot and boot continues non-fatally on failure
         * (AC-ERROR-1), so there is no retry that would repeat this leak,
         * but a driver acquire without a matching release on its own error
         * path is still worth avoiding on principle. */
        rmt_del_channel(s_led_chan);
        s_led_chan = NULL;
        return err;
    }

    err = rmt_enable(s_led_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(err));
        s_led_encoder->del(s_led_encoder);
        s_led_encoder = NULL;
        rmt_del_channel(s_led_chan);
        s_led_chan = NULL;
        return err;
    }

    return ESP_OK;
}

esp_err_t status_led_show(status_led_rgb_t frame) {
    /* WS2812 wire order is GRB, not RGB (see led_strip_encoder.c's bytes
     * encoder: "WS2812 transfer bit order: G7...G0R7...R0B7...B0"). A board
     * wired assuming RGB order would show the wrong hue for every state. */
    uint8_t grb[STATUS_LED_FRAME_BYTES] = {frame.g, frame.r, frame.b};

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    esp_err_t err = rmt_transmit(s_led_chan, s_led_encoder, grb, sizeof(grb), &tx_config);
    if (err != ESP_OK) {
        return err;
    }
    return rmt_tx_wait_all_done(s_led_chan, portMAX_DELAY);
}
