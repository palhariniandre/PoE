#!/usr/bin/env bash
set -euo pipefail

target="managed_components/espressif__esp_matter/connectedhomeip/connectedhomeip/src/platform/ESP32/NetworkCommissioningDriver_Ethernet.cpp"

if [[ ! -f "${target}" ]]; then
    echo "Arquivo nao encontrado: ${target}" >&2
    echo "Execute idf.py build uma vez para baixar os managed_components e tente novamente." >&2
    exit 1
fi

if grep -q "Using externally initialized SPI Ethernet interface" "${target}"; then
    echo "Patch ENC28J60/SPI Ethernet ja aplicado."
    exit 0
fi

git apply <<'PATCH'
diff --git a/managed_components/espressif__esp_matter/connectedhomeip/connectedhomeip/src/platform/ESP32/NetworkCommissioningDriver_Ethernet.cpp b/managed_components/espressif__esp_matter/connectedhomeip/connectedhomeip/src/platform/ESP32/NetworkCommissioningDriver_Ethernet.cpp
--- a/managed_components/espressif__esp_matter/connectedhomeip/connectedhomeip/src/platform/ESP32/NetworkCommissioningDriver_Ethernet.cpp
+++ b/managed_components/espressif__esp_matter/connectedhomeip/connectedhomeip/src/platform/ESP32/NetworkCommissioningDriver_Ethernet.cpp
@@ -17,6 +17,7 @@
 #include "esp_eth.h"
 #include "esp_eth_mac.h"
 #include "esp_eth_phy.h"
+#include "sdkconfig.h"
 #include <platform/ESP32/NetworkCommissioningDriver.h>
 
 using namespace ::chip;
@@ -41,6 +42,14 @@ static void on_eth_event(void * esp_netif, esp_event_base_t event_base, int32_t
 
 CHIP_ERROR ESPEthernetDriver::Init(NetworkStatusChangeCallback * networkStatusChangeCallback)
 {
+#if CONFIG_ETH_USE_SPI_ETHERNET && !CONFIG_ETH_USE_ESP32_EMAC
+    /* ESP-Matter v1.3's default Ethernet NetworkCommissioning driver is for the
+     * ESP32 internal EMAC + IP101 PHY path. This project brings up ENC28J60 SPI
+     * Ethernet before esp_matter::start(), so there is no second driver to install here.
+     */
+    ChipLogProgress(DeviceLayer, "Using externally initialized SPI Ethernet interface");
+    return CHIP_NO_ERROR;
+#else
     /* Currently default ethernet board supported is IP101, if you want to use other types of
      * ethernet board then you can override this function in your application. */
 
@@ -71,6 +80,7 @@ CHIP_ERROR ESPEthernetDriver::Init(NetworkStatusChangeCallback * networkStatusCh
     ESP_ERROR_CHECK(esp_eth_start(eth_handle));
 
     return CHIP_NO_ERROR;
+#endif
 }
 
 } // namespace NetworkCommissioning
PATCH

echo "Patch ENC28J60/SPI Ethernet aplicado em ${target}."
