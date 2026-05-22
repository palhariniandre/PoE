#include "app_matter.h"

#include "app_led.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_console.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <lib/core/CHIPError.h>

static const char *TAG = "app_matter";
static uint16_t s_light_endpoint_id = 0;
static bool s_matter_initialized = false;

using namespace chip::app::Clusters;
using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;

static constexpr auto k_commissioning_window_timeout_seconds = 300;

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Matter: interface IP address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Matter: commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Matter: commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Matter: commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Matter: commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Matter: commissioning window closed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
        ESP_LOGI(TAG, "Matter: fabric removed");
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            chip::CommissioningWindowManager &commissioning_manager =
                chip::Server::GetInstance().GetCommissioningWindowManager();
            constexpr auto timeout =
                chip::System::Clock::Seconds16(k_commissioning_window_timeout_seconds);

            if (!commissioning_manager.IsCommissioningWindowOpen()) {
                CHIP_ERROR err = commissioning_manager.OpenBasicCommissioningWindow(
                    timeout, chip::CommissioningWindowAdvertisement::kDnssdOnly);
                if (err != CHIP_NO_ERROR) {
                    ESP_LOGE(TAG, "Failed to reopen commissioning window: %" CHIP_ERROR_FORMAT,
                             err.Format());
                }
            }
        }
        break;
    }

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGW(TAG, "Matter: fail-safe timer expired");
        break;

    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id,
                                       uint8_t effect_id, uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identify: endpoint=%u type=%u effect=%u variant=%u",
             endpoint_id, type, effect_id, effect_variant);
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id,
                                         uint32_t cluster_id, uint32_t attribute_id,
                                         esp_matter_attr_val_t *val, void *priv_data)
{
    if (type != PRE_UPDATE) {
        return ESP_OK;
    }

    if (endpoint_id == s_light_endpoint_id && cluster_id == OnOff::Id &&
        attribute_id == OnOff::Attributes::OnOff::Id) {
        bool on = val->val.b;
        ESP_LOGI(TAG, "Matter OnOff=%d -> GPIO26=%d", on, on ? 1 : 0);
        return app_led_set(on);
    }

    return ESP_OK;
}

esp_err_t app_matter_light_init(void)
{
    if (s_matter_initialized) {
        return ESP_OK;
    }

    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    if (!node) {
        ESP_LOGE(TAG, "Failed to create Matter root node");
        return ESP_FAIL;
    }

    on_off_light::config_t light_config;
    light_config.on_off.on_off = app_led_get();
    light_config.on_off.lighting.start_up_on_off = nullptr;

    endpoint_t *endpoint = on_off_light::create(node, &light_config, ENDPOINT_FLAG_NONE, nullptr);
    if (!endpoint) {
        ESP_LOGE(TAG, "Failed to create Matter On/Off Light endpoint");
        return ESP_FAIL;
    }

    s_light_endpoint_id = endpoint::get_id(endpoint);
    if (s_light_endpoint_id != 1) {
        ESP_LOGW(TAG, "Light endpoint is %u, expected 1 for the test commands", s_light_endpoint_id);
    }

    ESP_LOGI(TAG, "Matter On/Off Light criado no endpoint 0x%x", s_light_endpoint_id);
    s_matter_initialized = true;
    return ESP_OK;
}

esp_err_t app_matter_start(void)
{
    if (!s_matter_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter: %s", esp_err_to_name(err));
        return err;
    }

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::init();
#endif

    ESP_LOGI(TAG, "Matter iniciado sobre a interface Ethernet ja validada");
    return ESP_OK;
}

uint16_t app_matter_light_endpoint_id(void)
{
    return s_light_endpoint_id;
}
