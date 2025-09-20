#pragma once
#include <stddef.h>
#include <stdint.h>
#include "connectors.h"

typedef enum {
  KIND_HTTP,
  KIND_MODBUS_RTU,
  KIND_MODBUS_TCP,
  KIND_MQTT,
  KIND_SPI,
  KIND_COAP,
  KIND_I2C,
  KIND_LORAWAN,
  KIND_ONEWIRE,
  KIND_OPCUA,
  KIND_SOCKETCAN,
  KIND_ZIGBEE,
  KIND_UART,
  KIND_BLE
} kind_t;

typedef struct {
  const uint8_t* data;
  size_t len;            // binaire-safe
  int is_text;           // hint
  const char* content_type; // ex: "application/json"
} gw_payload_t;

typedef struct {
  kind_t protocole;    
  union {                     
    http_server_params_t http_server;
    modbus_rtu_params_t  modbus_rtu;
    modbus_tcp_params_t  modbus_tcp;
    mqtt_params_t        mqtt;
    spi_params_t         spi;
    coap_params_t        coap;
    i2c_params_t         i2c;
    lorawan_params_t     lorawan;
    onewire_params_t     onewire;
    opcua_params_t       opcua;
    socketcan_params_t   socketcan;
    zigbee_params_t      zigbee;
    uart_params_t        uart;
    ble_params_t         ble;
  } params;
  gw_payload_t pl;        // quoi envoyer
} gw_msg_t;

typedef int (*gw_send_fn)(const gw_msg_t* out, void* ctx);          
typedef int (*gw_transform_fn)(const gw_msg_t* in, gw_msg_t* out, void* user);
