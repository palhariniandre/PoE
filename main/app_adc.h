#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int input_raw;
    int output_raw;
    uint32_t input_adc_mv;
    uint32_t output_adc_mv;
    uint32_t input_voltage_mv;
    uint32_t output_voltage_mv;
    bool input_saturated;
    bool output_saturated;
} app_adc_reading_t;

typedef void (*app_adc_update_cb_t)(const app_adc_reading_t *reading, void *context);

esp_err_t app_adc_init(void);
esp_err_t app_adc_start(app_adc_update_cb_t callback, void *context);
esp_err_t app_adc_get_latest(app_adc_reading_t *reading);

#ifdef __cplusplus
}
#endif
