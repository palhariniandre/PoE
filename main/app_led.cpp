#include "app_led.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "app_led";
static constexpr gpio_num_t APP_LED_GPIO = GPIO_NUM_26;

static bool s_led_on = false;

esp_err_t app_led_init(void)
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = 1ULL << APP_LED_GPIO;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "failed to configure GPIO26");
    ESP_RETURN_ON_ERROR(app_led_set(false), TAG, "failed to set initial LED state");

    ESP_LOGI(TAG, "LED de teste configurado no GPIO%d", APP_LED_GPIO);
    return ESP_OK;
}

esp_err_t app_led_set(bool on)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(APP_LED_GPIO, on ? 1 : 0), TAG, "failed to set LED level");
    s_led_on = on;
    return ESP_OK;
}

bool app_led_get(void)
{
    return s_led_on;
}
