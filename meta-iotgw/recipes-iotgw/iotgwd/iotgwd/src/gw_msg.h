#pragma once
#include <stddef.h>
#include <stdint.h>

typedef enum { GW_ADDR_STR, GW_ADDR_MODBUS, GW_ADDR_CAN } gw_addr_kind_t;

typedef struct {
  gw_addr_kind_t kind;
  union {
    struct { const char* s; } str;                 // topic MQTT, URL HTTP/CoAP, …
    struct { uint8_t slave_id, fc; uint16_t addr, len; } modbus;
    struct { uint32_t can_id; } can;
  } u;
} gw_addr_t;

typedef struct {
  const uint8_t* data; size_t len;                 // binaire-safe
  int is_text;                                     // hint
  const char* content_type;                        // ex: "application/json"
} gw_payload_t;

typedef struct {
  // Pour ce refacto, on reste simple : on normalise juste la "cible" + payload
  gw_addr_t   dst;                                 // où envoyer (topic/url/id…)
  gw_payload_t pl;                                 // quoi envoyer
  // Méta optionnelles
  const char* method;                              // ex: "POST" pour HTTP client
  int qos, retain;                                 // MQTT
} gw_msg_t;

typedef int (*gw_send_fn)(const gw_msg_t* out, void* ctx);          // envoi abstrait
typedef int (*gw_transform_fn)(const gw_msg_t* in, gw_msg_t* out, void* user); // mapping
