#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_matter.h"

esp_err_t app_voltage_measurement_create_endpoints(esp_matter::node_t *node);
esp_err_t app_voltage_measurement_start(void);
uint16_t app_voltage_measurement_input_endpoint_id(void);
uint16_t app_voltage_measurement_output_endpoint_id(void);
