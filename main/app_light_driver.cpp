#include "app_light_driver.h"

#include <inttypes.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "app_light";

static constexpr gpio_num_t APP_LIGHT_PWM_GPIO = GPIO_NUM_26;
static constexpr ledc_mode_t APP_LIGHT_PWM_MODE = LEDC_LOW_SPEED_MODE;
static constexpr ledc_timer_t APP_LIGHT_PWM_TIMER = LEDC_TIMER_0;
static constexpr ledc_channel_t APP_LIGHT_PWM_CHANNEL = LEDC_CHANNEL_0;
static constexpr ledc_timer_bit_t APP_LIGHT_PWM_RESOLUTION = LEDC_TIMER_10_BIT;
static constexpr uint32_t APP_LIGHT_PWM_FREQ_HZ = 20000;
static constexpr uint32_t APP_LIGHT_PWM_MAX_DUTY = (1U << 10) - 1;
static constexpr uint8_t APP_LIGHT_MAX_MATTER_LEVEL = 254;
static constexpr uint8_t APP_LIGHT_DEFAULT_LEVEL = 64;

static bool s_initialized = false;
static bool s_on = false;
static uint8_t s_level = APP_LIGHT_DEFAULT_LEVEL;
static uint32_t s_duty = 0;

static uint32_t level_to_duty(uint8_t level)
{
    if (level == 0) {
        return 0;
    }
    if (level > APP_LIGHT_MAX_MATTER_LEVEL) {
        level = APP_LIGHT_MAX_MATTER_LEVEL;
    }

    uint32_t duty = (static_cast<uint32_t>(level) * APP_LIGHT_PWM_MAX_DUTY +
                     (APP_LIGHT_MAX_MATTER_LEVEL / 2)) /
                    APP_LIGHT_MAX_MATTER_LEVEL;
    return duty == 0 ? 1 : duty;
}

static esp_err_t apply_pwm(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "PWM ainda nao inicializado");

    s_duty = s_on ? level_to_duty(s_level) : 0;
    ESP_RETURN_ON_ERROR(ledc_set_duty(APP_LIGHT_PWM_MODE, APP_LIGHT_PWM_CHANNEL, s_duty),
                        TAG, "falha ao ajustar duty do PWM");
    ESP_RETURN_ON_ERROR(ledc_update_duty(APP_LIGHT_PWM_MODE, APP_LIGHT_PWM_CHANNEL),
                        TAG, "falha ao aplicar duty do PWM");

    ESP_LOGI(TAG, "PWM lampada: on=%d level=%u duty=%" PRIu32 "/%" PRIu32,
             s_on, s_level, s_duty, APP_LIGHT_PWM_MAX_DUTY);
    return ESP_OK;
}

esp_err_t app_light_driver_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_config = {};
    timer_config.speed_mode = APP_LIGHT_PWM_MODE;
    timer_config.timer_num = APP_LIGHT_PWM_TIMER;
    timer_config.duty_resolution = APP_LIGHT_PWM_RESOLUTION;
    timer_config.freq_hz = APP_LIGHT_PWM_FREQ_HZ;
    timer_config.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "falha ao configurar timer LEDC");

    ledc_channel_config_t channel_config = {};
    channel_config.gpio_num = APP_LIGHT_PWM_GPIO;
    channel_config.speed_mode = APP_LIGHT_PWM_MODE;
    channel_config.channel = APP_LIGHT_PWM_CHANNEL;
    channel_config.intr_type = LEDC_INTR_DISABLE;
    channel_config.timer_sel = APP_LIGHT_PWM_TIMER;
    channel_config.duty = 0;
    channel_config.hpoint = 0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG, "falha ao configurar canal LEDC");

    s_initialized = true;
    s_on = false;
    s_duty = 0;

    ESP_LOGI(TAG, "PWM local configurado no GPIO%d: %" PRIu32 " Hz, 10 bits",
             APP_LIGHT_PWM_GPIO, APP_LIGHT_PWM_FREQ_HZ);
    return apply_pwm();
}

esp_err_t app_light_driver_set_on(bool on)
{
    s_on = on;
    return apply_pwm();
}

esp_err_t app_light_driver_set_level(uint8_t level)
{
    if (level > APP_LIGHT_MAX_MATTER_LEVEL) {
        level = APP_LIGHT_MAX_MATTER_LEVEL;
    }
    s_level = level;
    return apply_pwm();
}

bool app_light_driver_is_on(void)
{
    return s_on;
}

uint8_t app_light_driver_get_level(void)
{
    return s_level;
}

uint32_t app_light_driver_get_duty(void)
{
    return s_duty;
}
