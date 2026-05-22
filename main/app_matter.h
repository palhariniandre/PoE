#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_matter_light_init(void);
esp_err_t app_matter_start(void);
uint16_t app_matter_light_endpoint_id(void);

#ifdef __cplusplus
}
#endif
