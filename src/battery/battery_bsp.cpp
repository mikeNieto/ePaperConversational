#include <stdio.h>
#include <stdint.h>
#include "battery_bsp.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "user_config.h"

static const char *TAG_BAT = "battery";

static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t cali_handle;
static bool initialized = false;
static float last_voltage = 0.0f;

struct BatteryCurvePoint {
    float voltage;
    uint8_t percentage;
};

/* A LiPo voltage curve is a better estimate than a single linear mapping. */
static const BatteryCurvePoint battery_curve[] = {
    { BATTERY_FULL_VOLTAGE, 100 },
    { 4.08f, 95 },
    { 4.02f, 90 },
    { 3.95f, 80 },
    { 3.88f, 70 },
    { 3.82f, 60 },
    { 3.76f, 50 },
    { 3.70f, 40 },
    { 3.65f, 30 },
    { 3.55f, 15 },
    { BATTERY_EMPTY_VOLTAGE, 0 },
};

static bool battery_read_average_voltage(float* voltage)
{
    if (!initialized || !voltage) return false;

    int samples[BATTERY_SAMPLE_COUNT];
    size_t sample_count = 0;

    for (int i = 0; i < BATTERY_SAMPLE_COUNT; i++) {
        int raw = 0;
        int mv = 0;
        if (adc_oneshot_read(adc1_handle, ADC_CHANNEL_3, &raw) == ESP_OK &&
            adc_cali_raw_to_voltage(cali_handle, raw, &mv) == ESP_OK) {
            samples[sample_count++] = mv;
        }
        if (i + 1 < BATTERY_SAMPLE_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(BATTERY_SAMPLE_DELAY_MS));
        }
    }

    if (sample_count == 0) return false;

    /* Sort the small sample set so isolated ADC spikes can be discarded. */
    for (size_t i = 1; i < sample_count; i++) {
        int value = samples[i];
        size_t j = i;
        while (j > 0 && samples[j - 1] > value) {
            samples[j] = samples[j - 1];
            j--;
        }
        samples[j] = value;
    }

    size_t trim = 0;
    if (sample_count > (size_t)(BATTERY_TRIM_SAMPLE_COUNT * 2)) {
        trim = BATTERY_TRIM_SAMPLE_COUNT;
    }

    uint32_t sum_mv = 0;
    for (size_t i = trim; i < sample_count - trim; i++) {
        sum_mv += (uint32_t)samples[i];
    }

    size_t used_samples = sample_count - (trim * 2);
    *voltage = ((float)sum_mv / (float)used_samples) * 0.001f * BATTERY_DIVIDER_RATIO;
    return true;
}

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
    float voltage = 0.0f;
    if (battery_read_average_voltage(&voltage)) {
        last_voltage = voltage;
    }
    return last_voltage;
}

int battery_voltage_to_percentage(float voltage)
{
    const size_t point_count = sizeof(battery_curve) / sizeof(battery_curve[0]);
    if (voltage >= battery_curve[0].voltage) return battery_curve[0].percentage;
    if (voltage <= battery_curve[point_count - 1].voltage) return battery_curve[point_count - 1].percentage;

    for (size_t i = 1; i < point_count; i++) {
        const BatteryCurvePoint& upper = battery_curve[i - 1];
        const BatteryCurvePoint& lower = battery_curve[i];
        if (voltage >= lower.voltage) {
            float fraction = (voltage - lower.voltage) / (upper.voltage - lower.voltage);
            float percentage = lower.percentage + fraction * (upper.percentage - lower.percentage);
            return (int)(percentage + 0.5f);
        }
    }

    return 0;
}

int battery_get_percentage(void)
{
    return battery_voltage_to_percentage(battery_get_voltage());
}

void battery_get_status(float* voltage, int* percentage)
{
    float measured_voltage = battery_get_voltage();
    if (voltage) *voltage = measured_voltage;
    if (percentage) *percentage = battery_voltage_to_percentage(measured_voltage);
}
