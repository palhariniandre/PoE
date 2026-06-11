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
static uint8_t s_current_level = 64;
static uint16_t s_current_x = 0x616b;
static uint16_t s_current_y = 0x607d;
static uint16_t s_color_temperature_mireds = 250;

using namespace chip::app::Clusters;
using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;

static constexpr auto k_commissioning_window_timeout_seconds = 300;

static void set_deferred_attribute_persistence(endpoint_t *ep, uint32_t cluster_id, uint32_t attribute_id)
{
    cluster_t *cluster = esp_matter::cluster::get(ep, cluster_id);
    if (!cluster) {
        ESP_LOGW(TAG, "Cluster 0x%lx nao encontrado no endpoint 0x%x", cluster_id, endpoint::get_id(ep));
        return;
    }

    attribute_t *attribute = attribute::get(cluster, attribute_id);
    if (!attribute) {
        ESP_LOGW(TAG, "Atributo 0x%lx nao encontrado no cluster 0x%lx", attribute_id, cluster_id);
        return;
    }

    esp_err_t err = attribute::set_deferred_persistence(attribute);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao configurar persistencia do atributo 0x%lx: %s",
                 attribute_id, esp_err_to_name(err));
    }
}

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

    if (endpoint_id != s_light_endpoint_id) {
        return ESP_OK;
    }

    if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
        ESP_LOGI(TAG, "Matter OnOff=%d -> GPIO26=%d", val->val.b, val->val.b ? 1 : 0);
        return app_led_set(val->val.b);
    }

    if (cluster_id == LevelControl::Id && attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
        s_current_level = val->val.u8;
        ESP_LOGI(TAG, "Matter CurrentLevel=%u", s_current_level);
        return ESP_OK;
    }

    if (cluster_id == ColorControl::Id) {
        if (attribute_id == ColorControl::Attributes::ColorTemperatureMireds::Id) {
            s_color_temperature_mireds = val->val.u16;
            ESP_LOGI(TAG, "Matter ColorTemperatureMireds=%u", s_color_temperature_mireds);
        } else if (attribute_id == ColorControl::Attributes::CurrentX::Id) {
            s_current_x = val->val.u16;
            ESP_LOGI(TAG, "Matter CurrentX=%u CurrentY=%u", s_current_x, s_current_y);
        } else if (attribute_id == ColorControl::Attributes::CurrentY::Id) {
            s_current_y = val->val.u16;
            ESP_LOGI(TAG, "Matter CurrentX=%u CurrentY=%u", s_current_x, s_current_y);
        }
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

    extended_color_light::config_t light_config;
    light_config.on_off.on_off = app_led_get();
    light_config.on_off.lighting.start_up_on_off = nullptr;
    light_config.level_control.current_level = s_current_level;
    light_config.level_control.on_level = s_current_level;
    light_config.level_control.lighting.start_up_current_level = s_current_level;
    light_config.color_control.color_mode = static_cast<uint8_t>(ColorControl::ColorMode::kColorTemperature);
    light_config.color_control.enhanced_color_mode =
        static_cast<uint8_t>(ColorControl::ColorMode::kColorTemperature);
    light_config.color_control.color_temperature.color_temperature_mireds = s_color_temperature_mireds;
    light_config.color_control.color_temperature.startup_color_temperature_mireds = nullptr;
    light_config.color_control.xy.current_x = s_current_x;
    light_config.color_control.xy.current_y = s_current_y;

    endpoint_t *endpoint = extended_color_light::create(node, &light_config, ENDPOINT_FLAG_NONE, nullptr);
    if (!endpoint) {
        ESP_LOGE(TAG, "Failed to create Matter Extended Color Light endpoint");
        return ESP_FAIL;
    }

    s_light_endpoint_id = endpoint::get_id(endpoint);
    if (s_light_endpoint_id != 1) {
        ESP_LOGW(TAG, "Light endpoint is %u, expected 1 for the test commands", s_light_endpoint_id);
    }

    set_deferred_attribute_persistence(endpoint, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    set_deferred_attribute_persistence(endpoint, ColorControl::Id, ColorControl::Attributes::CurrentX::Id);
    set_deferred_attribute_persistence(endpoint, ColorControl::Id, ColorControl::Attributes::CurrentY::Id);
    set_deferred_attribute_persistence(endpoint, ColorControl::Id,
                                       ColorControl::Attributes::ColorTemperatureMireds::Id);

    ESP_LOGI(TAG, "Matter Extended Color Light criado no endpoint 0x%x", s_light_endpoint_id);
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
