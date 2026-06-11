#pragma once

#include "esp_err.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_eth_init(void);
esp_err_t app_wait_for_eth_connected(TickType_t timeout_ticks);
bool app_eth_is_initialized(void);
esp_netif_t *app_eth_get_netif(void);
esp_eth_handle_t app_eth_get_handle(void);
esp_err_t app_eth_ensure_ipv6_linklocal(void);

#ifdef __cplusplus
}
#endif
