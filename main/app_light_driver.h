#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_light_driver_init(void);
esp_err_t app_light_driver_set_on(bool on);
esp_err_t app_light_driver_set_level(uint8_t level);

bool app_light_driver_is_on(void);
uint8_t app_light_driver_get_level(void);
uint32_t app_light_driver_get_duty(void);

#ifdef __cplusplus
}
#endif
