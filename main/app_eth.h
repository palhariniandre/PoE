#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_eth_init(void);
esp_err_t app_wait_for_eth_connected(TickType_t timeout_ticks);

#ifdef __cplusplus
}
#endif
