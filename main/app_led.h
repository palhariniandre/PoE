#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_led_init(void);
esp_err_t app_led_set(bool on);
bool app_led_get(void);

#ifdef __cplusplus
}
#endif
