#include "app_voltage_measurement.h"

#include "app_adc.h"

#include <memory>

#include "app-common/zap-generated/ids/Attributes.h"
#include "app/clusters/electrical-power-measurement-server/electrical-power-measurement-server.h"
#include "app/reporting/reporting.h"
#include "esp_log.h"
#include "lib/support/BitMask.h"
#include "platform/CHIPDeviceLayer.h"

static const char *TAG = "app_voltage_matter";

static constexpr int64_t APP_INPUT_MAX_MV = 48000;
static constexpr int64_t APP_OUTPUT_MAX_MV = 24000;
static constexpr uint32_t APP_MATTER_REPORT_RESOLUTION_MV = 100;

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::ElectricalPowerMeasurement;
using namespace esp_matter;

class VoltageMeasurementDelegate : public ElectricalPowerMeasurement::Delegate {
public:
    explicit VoltageMeasurementDelegate(int64_t max_voltage_mv) : m_max_voltage_mv(max_voltage_mv)
    {
        m_accuracy_range.rangeMin = 0;
        m_accuracy_range.rangeMax = max_voltage_mv;
        m_accuracy_range.percentMax = MakeOptional(static_cast<Percent100ths>(1000));
        m_accuracy_range.percentTypical = MakeOptional(static_cast<Percent100ths>(500));
    }

    PowerModeEnum GetPowerMode() override { return PowerModeEnum::kDc; }
    uint8_t GetNumberOfMeasurementTypes() override { return 1; }

    CHIP_ERROR StartAccuracyRead() override { return CHIP_NO_ERROR; }

    CHIP_ERROR GetAccuracyByIndex(uint8_t index, Structs::MeasurementAccuracyStruct::Type &accuracy) override
    {
        if (index != 0) {
            return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
        }

        accuracy.measurementType = MeasurementTypeEnum::kVoltage;
        accuracy.measured = true;
        accuracy.minMeasuredValue = 0;
        accuracy.maxMeasuredValue = m_max_voltage_mv;
        accuracy.accuracyRanges = DataModel::List<const Structs::MeasurementAccuracyRangeStruct::Type>(
            &m_accuracy_range, 1);
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR EndAccuracyRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR StartRangesRead() override { return CHIP_NO_ERROR; }

    CHIP_ERROR GetRangeByIndex(uint8_t index, Structs::MeasurementRangeStruct::Type &range) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }

    CHIP_ERROR EndRangesRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR StartHarmonicCurrentsRead() override { return CHIP_NO_ERROR; }

    CHIP_ERROR GetHarmonicCurrentsByIndex(uint8_t index, Structs::HarmonicMeasurementStruct::Type &measurement) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }

    CHIP_ERROR EndHarmonicCurrentsRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR StartHarmonicPhasesRead() override { return CHIP_NO_ERROR; }

    CHIP_ERROR GetHarmonicPhasesByIndex(uint8_t index, Structs::HarmonicMeasurementStruct::Type &measurement) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }

    CHIP_ERROR EndHarmonicPhasesRead() override { return CHIP_NO_ERROR; }

    DataModel::Nullable<int64_t> GetVoltage() override { return m_voltage; }
    DataModel::Nullable<int64_t> GetActiveCurrent() override { return {}; }
    DataModel::Nullable<int64_t> GetReactiveCurrent() override { return {}; }
    DataModel::Nullable<int64_t> GetApparentCurrent() override { return {}; }
    DataModel::Nullable<int64_t> GetActivePower() override { return {}; }
    DataModel::Nullable<int64_t> GetReactivePower() override { return {}; }
    DataModel::Nullable<int64_t> GetApparentPower() override { return {}; }
    DataModel::Nullable<int64_t> GetRMSVoltage() override { return {}; }
    DataModel::Nullable<int64_t> GetRMSCurrent() override { return {}; }
    DataModel::Nullable<int64_t> GetRMSPower() override { return {}; }
    DataModel::Nullable<int64_t> GetFrequency() override { return {}; }
    DataModel::Nullable<int64_t> GetPowerFactor() override { return {}; }
    DataModel::Nullable<int64_t> GetNeutralCurrent() override { return {}; }

    void SetVoltage(int64_t voltage_mv)
    {
        if (m_voltage.Update(DataModel::MakeNullable(voltage_mv))) {
            MatterReportingAttributeChangeCallback(mEndpointId, ElectricalPowerMeasurement::Id,
                                                   Attributes::Voltage::Id);
        }
    }

private:
    int64_t m_max_voltage_mv;
    Structs::MeasurementAccuracyRangeStruct::Type m_accuracy_range;
    DataModel::Nullable<int64_t> m_voltage;
};

static uint16_t s_input_endpoint_id;
static uint16_t s_output_endpoint_id;
static VoltageMeasurementDelegate s_input_delegate(APP_INPUT_MAX_MV);
static VoltageMeasurementDelegate s_output_delegate(APP_OUTPUT_MAX_MV);
static std::unique_ptr<ElectricalPowerMeasurement::Instance> s_input_instance;
static std::unique_ptr<ElectricalPowerMeasurement::Instance> s_output_instance;

static endpoint_t *create_voltage_endpoint(node_t *node)
{
    endpoint_t *endpoint = esp_matter::endpoint::create(node, ENDPOINT_FLAG_NONE, nullptr);
    if (!endpoint) {
        return nullptr;
    }

    esp_err_t err = esp_matter::endpoint::add_device_type(
        endpoint, esp_matter::endpoint::electrical_sensor::get_device_type_id(),
        esp_matter::endpoint::electrical_sensor::get_device_type_version());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao adicionar device type Electrical Sensor: %s", esp_err_to_name(err));
        return nullptr;
    }

    esp_matter::cluster::descriptor::config_t descriptor_config;
    if (!esp_matter::cluster::descriptor::create(endpoint, &descriptor_config, CLUSTER_FLAG_SERVER)) {
        return nullptr;
    }

    esp_matter::cluster::power_topology::config_t topology_config;
    if (!esp_matter::cluster::power_topology::create(
            endpoint, &topology_config, CLUSTER_FLAG_SERVER,
            esp_matter::cluster::power_topology::feature::set_topology::get_id())) {
        return nullptr;
    }

    esp_matter::cluster::electrical_power_measurement::config_t measurement_config;
    cluster_t *measurement_cluster = esp_matter::cluster::electrical_power_measurement::create(
        endpoint, &measurement_config, CLUSTER_FLAG_SERVER,
        esp_matter::cluster::electrical_power_measurement::feature::direct_current::get_id());
    if (!measurement_cluster) {
        return nullptr;
    }

    if (!esp_matter::cluster::electrical_power_measurement::attribute::create_voltage(
            measurement_cluster, nullable<int64_t>())) {
        ESP_LOGE(TAG, "Falha ao criar atributo Voltage");
        return nullptr;
    }

    return endpoint;
}

static uint32_t quantize_voltage(uint32_t voltage_mv)
{
    return ((voltage_mv + (APP_MATTER_REPORT_RESOLUTION_MV / 2)) /
            APP_MATTER_REPORT_RESOLUTION_MV) * APP_MATTER_REPORT_RESOLUTION_MV;
}

static void publish_voltage_work(intptr_t context)
{
    uint32_t packed = static_cast<uint32_t>(context);
    int64_t input_mv = static_cast<int64_t>((packed >> 16) & 0xffff);
    int64_t output_mv = static_cast<int64_t>(packed & 0xffff);
    s_input_delegate.SetVoltage(input_mv);
    s_output_delegate.SetVoltage(output_mv);
}

static void adc_update_callback(const app_adc_reading_t *reading, void *context)
{
    uint32_t input_mv = quantize_voltage(reading->input_voltage_mv);
    uint32_t output_mv = quantize_voltage(reading->output_voltage_mv);
    uint32_t packed = ((input_mv & 0xffff) << 16) | (output_mv & 0xffff);

    CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().ScheduleWork(
        publish_voltage_work, static_cast<intptr_t>(packed));
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Falha ao agendar publicacao ADC: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

esp_err_t app_voltage_measurement_create_endpoints(node_t *node)
{
    if (s_input_endpoint_id || s_output_endpoint_id) {
        return ESP_OK;
    }

    endpoint_t *input_endpoint = create_voltage_endpoint(node);
    if (!input_endpoint) {
        ESP_LOGE(TAG, "Falha ao criar endpoint da tensao de entrada");
        return ESP_FAIL;
    }
    s_input_endpoint_id = esp_matter::endpoint::get_id(input_endpoint);

    endpoint_t *output_endpoint = create_voltage_endpoint(node);
    if (!output_endpoint) {
        ESP_LOGE(TAG, "Falha ao criar endpoint da tensao de saida");
        return ESP_FAIL;
    }
    s_output_endpoint_id = esp_matter::endpoint::get_id(output_endpoint);

    ESP_LOGI(TAG, "Electrical Sensor entrada 48 V criado no endpoint 0x%x", s_input_endpoint_id);
    ESP_LOGI(TAG, "Electrical Sensor saida 24 V criado no endpoint 0x%x", s_output_endpoint_id);
    return ESP_OK;
}

esp_err_t app_voltage_measurement_start(void)
{
    if (!s_input_endpoint_id || !s_output_endpoint_id) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_input_instance || s_output_instance) {
        return ESP_OK;
    }

    BitMask<ElectricalPowerMeasurement::Feature> features(
        ElectricalPowerMeasurement::Feature::kDirectCurrent);
    BitMask<ElectricalPowerMeasurement::OptionalAttributes> optional_attributes(
        ElectricalPowerMeasurement::OptionalAttributes::kOptionalAttributeVoltage);

    s_input_instance = std::make_unique<ElectricalPowerMeasurement::Instance>(
        EndpointId(s_input_endpoint_id), s_input_delegate, features, optional_attributes);
    CHIP_ERROR err = s_input_instance->Init();
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Falha ao iniciar medicao da entrada: %" CHIP_ERROR_FORMAT, err.Format());
        s_input_instance.reset();
        return ESP_FAIL;
    }

    s_output_instance = std::make_unique<ElectricalPowerMeasurement::Instance>(
        EndpointId(s_output_endpoint_id), s_output_delegate, features, optional_attributes);
    err = s_output_instance->Init();
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Falha ao iniciar medicao da saida: %" CHIP_ERROR_FORMAT, err.Format());
        s_output_instance.reset();
        s_input_instance.reset();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Callbacks Matter das tensoes DC iniciados");
    return app_adc_start(adc_update_callback, nullptr);
}

uint16_t app_voltage_measurement_input_endpoint_id(void)
{
    return s_input_endpoint_id;
}

uint16_t app_voltage_measurement_output_endpoint_id(void)
{
    return s_output_endpoint_id;
}
