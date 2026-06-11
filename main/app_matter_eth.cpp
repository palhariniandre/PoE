#include "app_eth.h"

#include <esp_check.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/ESP32/NetworkCommissioningDriver.h>

using namespace chip;

namespace chip {
namespace DeviceLayer {
namespace NetworkCommissioning {

CHIP_ERROR ESPEthernetDriver::Init(NetworkStatusChangeCallback *networkStatusChangeCallback)
{
    (void)networkStatusChangeCallback;

    esp_err_t err = app_eth_init();
    if (err != ESP_OK) {
        ChipLogError(DeviceLayer, "ENC28J60 Ethernet init failed: %s", esp_err_to_name(err));
        return CHIP_ERROR_INTERNAL;
    }

    err = app_eth_ensure_ipv6_linklocal();
    if (err != ESP_OK) {
        ChipLogError(DeviceLayer, "ENC28J60 IPv6 link-local setup failed: %s", esp_err_to_name(err));
        return CHIP_ERROR_INTERNAL;
    }

    ChipLogProgress(DeviceLayer, "Matter NetworkCommissioning using ENC28J60 Ethernet");
    return CHIP_NO_ERROR;
}

} // namespace NetworkCommissioning
} // namespace DeviceLayer
} // namespace chip
