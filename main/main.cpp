#include "app_eth.h"
#include "app_led.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "main";

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    ESP_ERROR_CHECK(app_led_init());
    ESP_ERROR_CHECK(app_eth_init());
    ESP_ERROR_CHECK(app_wait_for_eth_connected(pdMS_TO_TICKS(30000)));

    ESP_LOGI(TAG, "Etapa 1 concluida: Ethernet ENC28J60 validada. Matter ainda nao iniciado.");
    ESP_LOGI(TAG, "Proxima etapa conceitual: app_matter_light_init() somente depois da rede pronta.");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
