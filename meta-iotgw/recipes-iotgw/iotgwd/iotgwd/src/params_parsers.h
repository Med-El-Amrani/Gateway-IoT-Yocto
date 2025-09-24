// src/params_parsers.h
#pragma once
#include <yaml.h>
#include "connectors.h"

// Per-type param parsers (EXPORTED; must be non-static in the .c that implements them)
int parse_mqtt_params(yaml_document_t* doc, yaml_node_t* params, mqtt_connector_t* out);
int parse_http_server_params(yaml_document_t* doc, yaml_node_t* params, http_server_connector_t* out);
int parse_modbus_rtu_params(yaml_document_t* doc, yaml_node_t* params, modbus_rtu_connector_t* out);
int parse_modbus_tcp_params(yaml_document_t* doc, yaml_node_t* params, modbus_tcp_connector_t* out);
int parse_uart_params(yaml_document_t* doc, yaml_node_t* params, uart_connector_t* out);
int parse_spi_params(yaml_document_t* doc, yaml_node_t* params, spi_connector_t* out);
