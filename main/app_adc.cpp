#include "app_adc.h"

#include <inttypes.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_adc";

static constexpr adc_channel_t APP_ADC_INPUT_CHANNEL = ADC_CHANNEL_7;  // GPIO35 / ADC1_CH7
static constexpr adc_channel_t APP_ADC_OUTPUT_CHANNEL = ADC_CHANNEL_6; // GPIO34 / ADC1_CH6
static constexpr adc_atten_t APP_ADC_ATTENUATION = ADC_ATTEN_DB_12;
static constexpr adc_bitwidth_t APP_ADC_BITWIDTH = ADC_BITWIDTH_DEFAULT;

static constexpr uint32_t APP_ADC_REFERENCE_MV = 3300;
static constexpr uint32_t APP_ADC_INPUT_FULL_SCALE_MV = 48000;
static constexpr uint32_t APP_ADC_OUTPUT_FULL_SCALE_MV = 24000;
static constexpr uint32_t APP_ADC_SAMPLE_COUNT = 32;
static constexpr TickType_t APP_ADC_SAMPLE_PERIOD = pdMS_TO_TICKS(1000);
static constexpr int APP_ADC_RAW_MAX = 4095;
static constexpr int APP_ADC_SATURATION_RAW = 4080;

static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_calibration_handle;
static bool s_calibration_enabled;
static bool s_initialized;
static TaskHandle_t s_adc_task_handle;
static app_adc_update_cb_t s_update_callback;
static void *s_update_context;
static portMUX_TYPE s_reading_lock = portMUX_INITIALIZER_UNLOCKED;
static app_adc_reading_t s_latest_reading;

static bool init_calibration(void)
{
    esp_err_t err = ESP_ERR_NOT_SUPPORTED;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t config = {};
    config.unit_id = ADC_UNIT_1;
    config.chan = APP_ADC_INPUT_CHANNEL;
    config.atten = APP_ADC_ATTENUATION;
    config.bitwidth = APP_ADC_BITWIDTH;
    err = adc_cali_create_scheme_curve_fitting(&config, &s_calibration_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t config = {};
    config.unit_id = ADC_UNIT_1;
    config.atten = APP_ADC_ATTENUATION;
    config.bitwidth = APP_ADC_BITWIDTH;
    config.default_vref = 1100;
    err = adc_cali_create_scheme_line_fitting(&config, &s_calibration_handle);
#endif

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Calibracao ADC habilitada");
        return true;
    }

    ESP_LOGW(TAG, "Calibracao ADC indisponivel (%s); usando conversao nominal", esp_err_to_name(err));
    s_calibration_handle = nullptr;
    return false;
}

static esp_err_t read_average(adc_channel_t channel, int *average_raw)
{
    ESP_RETURN_ON_FALSE(average_raw, ESP_ERR_INVALID_ARG, TAG, "average_raw is null");

    int64_t sum = 0;
    for (uint32_t i = 0; i < APP_ADC_SAMPLE_COUNT; ++i) {
        int raw = 0;
        ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc_handle, channel, &raw), TAG, "falha na leitura ADC");
        sum += raw;
    }

    *average_raw = static_cast<int>((sum + (APP_ADC_SAMPLE_COUNT / 2)) / APP_ADC_SAMPLE_COUNT);
    return ESP_OK;
}

static esp_err_t raw_to_adc_mv(int raw, uint32_t *adc_mv)
{
    ESP_RETURN_ON_FALSE(adc_mv, ESP_ERR_INVALID_ARG, TAG, "adc_mv is null");

    if (s_calibration_enabled) {
        int calibrated_mv = 0;
        ESP_RETURN_ON_ERROR(adc_cali_raw_to_voltage(s_calibration_handle, raw, &calibrated_mv),
                            TAG, "falha ao calibrar leitura ADC");
        *adc_mv = calibrated_mv > 0 ? static_cast<uint32_t>(calibrated_mv) : 0;
        return ESP_OK;
    }

    *adc_mv = (static_cast<uint32_t>(raw) * APP_ADC_REFERENCE_MV + (APP_ADC_RAW_MAX / 2)) /
              APP_ADC_RAW_MAX;
    return ESP_OK;
}

static uint32_t scale_voltage(uint32_t adc_mv, uint32_t full_scale_mv)
{
    if (adc_mv > APP_ADC_REFERENCE_MV) {
        adc_mv = APP_ADC_REFERENCE_MV;
    }
    return static_cast<uint32_t>((static_cast<uint64_t>(adc_mv) * full_scale_mv +
                                  (APP_ADC_REFERENCE_MV / 2)) /
                                 APP_ADC_REFERENCE_MV);
}

static esp_err_t sample_voltages(app_adc_reading_t *reading)
{
    ESP_RETURN_ON_FALSE(reading, ESP_ERR_INVALID_ARG, TAG, "reading is null");

    ESP_RETURN_ON_ERROR(read_average(APP_ADC_INPUT_CHANNEL, &reading->input_raw),
                        TAG, "falha no ADC de entrada");
    ESP_RETURN_ON_ERROR(read_average(APP_ADC_OUTPUT_CHANNEL, &reading->output_raw),
                        TAG, "falha no ADC de saida");
    ESP_RETURN_ON_ERROR(raw_to_adc_mv(reading->input_raw, &reading->input_adc_mv),
                        TAG, "falha na conversao da entrada");
    ESP_RETURN_ON_ERROR(raw_to_adc_mv(reading->output_raw, &reading->output_adc_mv),
                        TAG, "falha na conversao da saida");

    reading->input_voltage_mv = scale_voltage(reading->input_adc_mv, APP_ADC_INPUT_FULL_SCALE_MV);
    reading->output_voltage_mv = scale_voltage(reading->output_adc_mv, APP_ADC_OUTPUT_FULL_SCALE_MV);
    reading->input_saturated = reading->input_raw >= APP_ADC_SATURATION_RAW;
    reading->output_saturated = reading->output_raw >= APP_ADC_SATURATION_RAW;
    return ESP_OK;
}

static void adc_task(void *arg)
{
    TickType_t wake_time = xTaskGetTickCount();
    uint32_t log_counter = 0;

    while (true) {
        app_adc_reading_t reading = {};
        esp_err_t err = sample_voltages(&reading);
        if (err == ESP_OK) {
            taskENTER_CRITICAL(&s_reading_lock);
            s_latest_reading = reading;
            taskEXIT_CRITICAL(&s_reading_lock);

            if (s_update_callback) {
                s_update_callback(&reading, s_update_context);
            }

            if (++log_counter >= 5) {
                log_counter = 0;
                ESP_LOGI(TAG,
                         "Vin=%" PRIu32 ".%03" PRIu32 " V (GPIO35=%" PRIu32 " mV raw=%d), "
                         "Vout=%" PRIu32 ".%03" PRIu32 " V (GPIO34=%" PRIu32 " mV raw=%d)%s%s",
                         reading.input_voltage_mv / 1000, reading.input_voltage_mv % 1000,
                         reading.input_adc_mv, reading.input_raw,
                         reading.output_voltage_mv / 1000, reading.output_voltage_mv % 1000,
                         reading.output_adc_mv, reading.output_raw,
                         reading.input_saturated ? " INPUT_SATURATED" : "",
                         reading.output_saturated ? " OUTPUT_SATURATED" : "");
            }
        } else {
            ESP_LOGW(TAG, "Falha ao amostrar tensoes: %s", esp_err_to_name(err));
        }

        xTaskDelayUntil(&wake_time, APP_ADC_SAMPLE_PERIOD);
    }
}

esp_err_t app_adc_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    adc_oneshot_unit_init_cfg_t unit_config = {};
    unit_config.unit_id = ADC_UNIT_1;
    unit_config.ulp_mode = ADC_ULP_MODE_DISABLE;
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_config, &s_adc_handle), TAG, "falha ao inicializar ADC1");

    adc_oneshot_chan_cfg_t channel_config = {};
    channel_config.atten = APP_ADC_ATTENUATION;
    channel_config.bitwidth = APP_ADC_BITWIDTH;
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc_handle, APP_ADC_INPUT_CHANNEL, &channel_config),
                        TAG, "falha ao configurar GPIO35");
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc_handle, APP_ADC_OUTPUT_CHANNEL, &channel_config),
                        TAG, "falha ao configurar GPIO34");

    s_calibration_enabled = init_calibration();
    s_initialized = true;
    ESP_LOGI(TAG, "ADC configurado: entrada GPIO35=0..48 V, saida GPIO34=0..24 V");
    return ESP_OK;
}

esp_err_t app_adc_start(app_adc_update_cb_t callback, void *context)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "ADC nao inicializado");
    if (s_adc_task_handle) {
        return ESP_OK;
    }

    s_update_callback = callback;
    s_update_context = context;
    BaseType_t result = xTaskCreate(adc_task, "adc_voltage", 4096, nullptr, 4, &s_adc_task_handle);
    ESP_RETURN_ON_FALSE(result == pdPASS, ESP_ERR_NO_MEM, TAG, "falha ao criar tarefa ADC");
    return ESP_OK;
}

esp_err_t app_adc_get_latest(app_adc_reading_t *reading)
{
    ESP_RETURN_ON_FALSE(reading, ESP_ERR_INVALID_ARG, TAG, "reading is null");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "ADC nao inicializado");

    taskENTER_CRITICAL(&s_reading_lock);
    *reading = s_latest_reading;
    taskEXIT_CRITICAL(&s_reading_lock);
    return ESP_OK;
}
