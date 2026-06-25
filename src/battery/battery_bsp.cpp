#include <stdio.h>
#include "battery_bsp.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG_BAT = "battery";

static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t cali_handle;
static bool initialized = false;

void battery_init(void)
{
    adc_cali_curve_fitting_config_t cali_config = {};
    cali_config.unit_id = ADC_UNIT_1;
    cali_config.atten = ADC_ATTEN_DB_12;
    cali_config.bitwidth = ADC_BITWIDTH_12;
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle));

    adc_oneshot_unit_init_cfg_t init_config = {};
    init_config.unit_id = ADC_UNIT_1;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = ADC_ATTEN_DB_12;
    chan_cfg.bitwidth = ADC_BITWIDTH_12;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_3, &chan_cfg));

    initialized = true;
    ESP_LOGI(TAG_BAT, "ADC1_CH3 (GPIO4) initialized");
}

float battery_get_voltage(void)
{
    if (!initialized) return 0.0f;
    int raw = 0;
    int mv = 0;
    if (adc_oneshot_read(adc1_handle, ADC_CHANNEL_3, &raw) == ESP_OK) {
        adc_cali_raw_to_voltage(cali_handle, raw, &mv);
        return 0.001f * (float)mv * 2.0f;
    }
    return 0.0f;
}

int battery_get_percentage(void)
{
    float v = battery_get_voltage();
    if (v <= 3.0f) return 0;
    if (v >= 4.12f) return 100;
    int pct = (int)((v - 3.0f) / (4.12f - 3.0f) * 100.0f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}
