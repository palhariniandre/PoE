#include "app_matter.h"

#include "app_light_driver.h"

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

using namespace chip::app::Clusters;
using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;

static constexpr auto k_commissioning_window_timeout_seconds = 300;

static void add_root_network_info_clusters(node_t *node)
{
    endpoint_t *root_endpoint = endpoint::get(node, 0);
    if (!root_endpoint) {
        ESP_LOGW(TAG, "Endpoint 0 Root Node nao encontrado para clusters de rede");
        return;
    }

    if (!esp_matter::cluster::get(root_endpoint, EthernetNetworkDiagnostics::Id)) {
        esp_matter::cluster::diagnostics_network_ethernet::config_t ethernet_diag_config;
        cluster_t *ethernet_diag_cluster = esp_matter::cluster::diagnostics_network_ethernet::create(
            root_endpoint, &ethernet_diag_config, CLUSTER_FLAG_SERVER);
        if (!ethernet_diag_cluster) {
            ESP_LOGW(TAG, "Nao foi possivel adicionar Ethernet Network Diagnostics no endpoint 0");
            return;
        }
    }

    ESP_LOGI(TAG, "Endpoint 0 Root Node pronto com Network Commissioning e Ethernet Diagnostics");
}

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
        ESP_LOGI(TAG, "Matter OnOff=%d -> PWM", val->val.b);
        return app_light_driver_set_on(val->val.b);
    }

    if (cluster_id == LevelControl::Id && attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
        s_current_level = val->val.u8;
        ESP_LOGI(TAG, "Matter CurrentLevel=%u -> PWM duty", s_current_level);
        return app_light_driver_set_level(s_current_level);
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
    add_root_network_info_clusters(node);

    s_current_level = app_light_driver_get_level();

    dimmable_light::config_t light_config;
    light_config.on_off.on_off = app_light_driver_is_on();
    light_config.on_off.lighting.start_up_on_off = nullptr;
    light_config.level_control.current_level = s_current_level;
    light_config.level_control.on_level = s_current_level;
    light_config.level_control.lighting.start_up_current_level = s_current_level;

    endpoint_t *endpoint = dimmable_light::create(node, &light_config, ENDPOINT_FLAG_NONE, nullptr);
    if (!endpoint) {
        ESP_LOGE(TAG, "Failed to create Matter Dimmable Light endpoint");
        return ESP_FAIL;
    }

    s_light_endpoint_id = endpoint::get_id(endpoint);
    if (s_light_endpoint_id != 1) {
        ESP_LOGW(TAG, "Light endpoint is %u, expected 1 for the test commands", s_light_endpoint_id);
    }

    set_deferred_attribute_persistence(endpoint, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);

    ESP_LOGI(TAG, "Matter Dimmable Light criado no endpoint 0x%x", s_light_endpoint_id);
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
