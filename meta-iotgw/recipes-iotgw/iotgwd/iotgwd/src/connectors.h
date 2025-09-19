#ifndef CONNECTORS_H
#define CONNECTORS_H

#include <stdint.h>
#include <stdbool.h>

/* =========================
 * BLE (GATT) connector
 * =========================
 * adapter: "hciN" pattern (e.g., hci0)
 * device: MAC "XX:XX:XX:XX:XX:XX"
 * services[].uuid: string (min 4)
 * services[].characteristics[].mode: {"read","notify","write"}
 */
typedef enum {
    BLE_CHAR_MODE_READ,
    BLE_CHAR_MODE_NOTIFY,
    BLE_CHAR_MODE_WRITE
} ble_char_mode_t;

typedef struct {
    char *uuid;                 // min length 4
    int32_t interval_ms;        // [10..60000], optional: -1 if unset
    ble_char_mode_t mode;       // optional: default read if unset
    bool mode_set;              // track if 'mode' present
    bool interval_set;          // track if 'interval_ms' present
} ble_characteristic_t;

typedef struct {
    char *uuid;                         // min length 4
    size_t characteristics_count;
    ble_characteristic_t *characteristics; // optional
} ble_service_t;

typedef struct {
    char *adapter;   // pattern ^hci\d+$
    char *device;    // pattern MAC AA:BB:CC:DD:EE:FF
    size_t services_count;
    ble_service_t *services; // optional
} ble_params_t;

typedef struct {
    ble_params_t params;
} ble_connector_t;


/* =========================
 * HTTP server connector
 * =========================
 * bind: "[host]:port" or "*:port"
 * basic_auth: user, pass (optional)
 * tls: cert_file, key_file (optional)
 * routes[]: path "^/.*", method enum
 */
typedef enum {
    HTTP_METHOD_GET,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_PATCH,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_OPTIONS
} http_method_t;

typedef struct {
    char *user; // minLength 1
    char *pass; // minLength 1
    bool present;
} http_basic_auth_t;

typedef struct {
    char *cert_file;
    char *key_file;
    bool present;
} http_tls_t;

typedef struct {
    char *path;           // must start with '/'
    http_method_t method;
} http_route_t;

typedef struct {
    char *bind; // "^([^:\\s]+|\\*)?:\\d{2,5}$"
    http_basic_auth_t basic_auth; // optional (present==true if provided)
    http_tls_t tls;               // optional (present==true if provided)
    size_t routes_count;
    http_route_t *routes;         // optional
} http_server_params_t;

typedef struct {
    http_server_params_t params;
} http_server_connector_t;


/* =========================
 * Modbus RTU connector
 * =========================
 * port: "/dev/ttyS…" "/dev/ttyUSB…" "/dev/ttyAMA…" "/dev/ttyACM…"
 * baudrate: {1200..115200 enum}
 * parity: {"N","E","O"}
 * stopbits: {1,2}
 * timeout_ms: [50..5000]
 * rs485: rts_time_before_ms, rts_time_after_ms (optional, [0..50])
 * slaves[]: unit_id [1..247], poll_ms [100..60000]
 *   map[]: name (C identifier), func enum, addr [0..65535],
 *          count [1..4], type enum, optional scale, signed
 */
typedef enum { MODBUS_FUNC_HOLDING, MODBUS_FUNC_INPUT, MODBUS_FUNC_COIL, MODBUS_FUNC_DISCRETE } modbus_func_t;
typedef enum { MODBUS_TYPE_U16, MODBUS_TYPE_S16, MODBUS_TYPE_U32, MODBUS_TYPE_S32, MODBUS_TYPE_FLOAT, MODBUS_TYPE_DOUBLE } modbus_datatype_t;

typedef struct {
    char *name;           // ^[a-zA-Z_][a-zA-Z0-9_]*$
    modbus_func_t func;
    uint16_t addr;        // 0..65535
    uint8_t  count;       // 1..4
    modbus_datatype_t type;
    double   scale;       // optional; set has_scale=true if present
    bool     has_scale;
    bool     signed_flag; // optional (schema allows 'signed' boolean)
    bool     has_signed;
} modbus_point_t;

typedef struct {
    uint8_t unit_id; // 1..247
    uint32_t poll_ms; // 100..60000
    size_t map_count;
    modbus_point_t *map;
} modbus_slave_t;

typedef struct {
    int32_t rts_time_before_ms; // 0..50
    int32_t rts_time_after_ms;  // 0..50
    bool present;
} modbus_rs485_t;

typedef struct {
    char *port;         // pattern matches supported tty nodes
    int32_t baudrate;   // enum
    char parity;        // 'N'|'E'|'O'
    int32_t stopbits;   // 1|2
    int32_t timeout_ms; // 50..5000
    modbus_rs485_t rs485; // optional (present==true if provided)
    size_t slaves_count;
    modbus_slave_t *slaves; // minItems 1
} modbus_rtu_params_t;

typedef struct {
    modbus_rtu_params_t params;
} modbus_rtu_connector_t;


/* =========================
 * Modbus TCP connector
 * =========================
 * host: hostname or IPv4
 * port: [1..65535] default 502
 * unit_id: [0..255] default 1
 * timeout_ms: [50..5000]
 * retries: [0..10] default 2
 * map[]: same structure as RTU map
 */
typedef struct {
    char *name;             // ^[a-zA-Z_][a-zA-Z0-9_]*$
    modbus_func_t func;     // holding|input|coil|discrete
    uint16_t addr;          // 0..65535
    uint8_t  count;         // 1..4
    modbus_datatype_t type; // u16|s16|u32|s32|float|double
    double   scale;         // optional; has_scale=true if present
    bool     has_scale;
    bool     signed_flag;   // optional
    bool     has_signed;
} modbus_tcp_point_t;

typedef struct {
    char *host;         // hostname or IPv4
    uint16_t port;      // default 502
    uint8_t  unit_id;   // default 1
    int32_t timeout_ms; // 50..5000
    int32_t retries;    // 0..10 (default 2)
    size_t map_count;   // minItems 1
    modbus_tcp_point_t *map;
    bool port_set, unit_id_set, retries_set; // to track presence vs defaults
} modbus_tcp_params_t;

typedef struct {
    modbus_tcp_params_t params;
} modbus_tcp_connector_t;


/* =========================
 * MQTT connector
 * =========================
 * NOTE: Schema requires ["url","client_id"] but defines host/port instead;
 * treat URL as optional and favor host/port fields in C model.
 * client_id: [1..64]
 * clean_session (default true), keepalive_s [10..600],
 * qos {0,1,2} default 1, retain default false,
 * tls: enabled, ca/cert/key, insecure_skip_verify
 * topics[]: topic string, qos {0,1,2}
 */
typedef struct {
    bool enabled;                 // optional
    char *ca_file;                // optional
    char *cert_file;              // optional
    char *key_file;               // optional
    bool insecure_skip_verify;    // optional
    bool present;
} mqtt_tls_t;

typedef struct {
    char *topic;  // minLength 1
    int   qos;    // 0|1|2
    bool  qos_set;
} mqtt_topic_t;

typedef struct {
    // Either url OR (host,port). Support both.
    char *url;         // optional (see schema note)
    char *host;        // optional
    int   port;        // optional
    char *client_id;   // required, 1..64
    bool  clean_session; // default true
    bool  clean_session_set;
    int   keepalive_s;   // [10..600]
    bool  keepalive_set;
    int   qos;           // 0|1|2 default 1
    bool  qos_set;
    bool  retain;        // default false
    bool  retain_set;
    char *username;      // optional
    char *password;      // optional
    mqtt_tls_t tls;      // optional (present==true if provided)
    size_t topics_count;
    mqtt_topic_t *topics; // optional
} mqtt_params_t;

typedef struct {
    mqtt_params_t params;
} mqtt_connector_t;


/* =========================
 * SPI (spidev) connector
 * =========================
 * device: "/dev/spidev<BUS>.<CS>"
 * mode: enum {0,1,2,3} (default 0)
 * bits_per_word: {8,16,32} (default 8)
 * speed_hz: [1000..50000000] (default 1000000)
 * lsb_first, cs_change: bool (defaults false)
 * transactions[]: op {"read","write","transfer"}, len [1..4096],
 *                 tx hex string (optional), rx_len [1..4096] (optional)
 */
typedef enum { SPI_OP_READ, SPI_OP_WRITE, SPI_OP_TRANSFER } spi_op_t;

typedef struct {
    spi_op_t op;
    uint16_t len;        // [1..4096]
    char *tx;            // optional hex string "0x..." or raw hex
    uint16_t rx_len;     // optional [1..4096]
    bool has_tx;
    bool has_rx_len;
} spi_transaction_t;

typedef struct {
    char *device;        // ^/dev/spidev\d+\.\d+$
    int mode;            // 0..3 (default 0)
    int bits_per_word;   // 8|16|32 (default 8)
    int speed_hz;        // [1000..50000000] (default 1000000)
    bool lsb_first;      // default false
    bool cs_change;      // default false
    size_t transactions_count;
    spi_transaction_t *transactions; // optional
    bool mode_set, bpw_set, speed_set, lsb_first_set, cs_change_set;
} spi_params_t;

typedef struct {
    spi_params_t params;
} spi_connector_t;

/* =========================
 * CoAP server/client
 * =========================
 * params.bind: "host:port", "[ipv6]:port", "ip:port" ou "*:port"
 * params.dtls: soit PSK (psk_id, psk_key hex 16..64), soit Certs (cert_file,key_file,ca_file)
 */
typedef enum {
    COAP_DTLS_NONE = 0,
    COAP_DTLS_PSK,
    COAP_DTLS_CERTS
} coap_dtls_mode_t;

typedef struct {
    // PSK
    char *psk_id;   // minLength 1
    char *psk_key;  // hex ^[0-9A-Fa-f]{16,64}$
    // CERTS
    char *cert_file;
    char *key_file;
    char *ca_file;
    coap_dtls_mode_t mode; // NONE/PSK/CERTS
} coap_dtls_t;

typedef struct {
    char *bind;     // ^(\*|\[[0-9a-fA-F:]+\]|\d{1,3}(\.\d{1,3}){3}|[^\s:]+)?:\d{2,5}$
    bool has_dtls;
    coap_dtls_t dtls; // optional
} coap_params_t;

typedef struct {
    coap_params_t params;
} coap_connector_t;


/* =========================
 * I2C
 * =========================
 * bus: [0..32]
 * speed_hz: {100k, 400k, 1M} (optional)
 * devices[]: addr [0x03..0x77], name?, map[]
 * map[]: reg [0..255], len [1..16], type enum, endianness {be,le}=be,
 *        scale?, writable=false
 */
typedef enum { I2C_TYPE_U8, I2C_TYPE_S8, I2C_TYPE_U16, I2C_TYPE_S16, I2C_TYPE_U24, I2C_TYPE_U32, I2C_TYPE_S32, I2C_TYPE_FLOAT, I2C_TYPE_BYTES } i2c_point_type_t;
typedef enum { I2C_BE, I2C_LE } i2c_endianness_t;

typedef struct {
    uint8_t reg;          // 0..255
    uint8_t len;          // 1..16
    i2c_point_type_t type;
    i2c_endianness_t endianness; // default BE
    bool endianness_set;
    double scale;         // optional
    bool has_scale;
    bool writable;        // default false
    bool writable_set;
} i2c_map_point_t;

typedef struct {
    uint8_t addr;     // 0x03..0x77
    char *name;       // optional
    size_t map_count; // optional
    i2c_map_point_t *map;
} i2c_device_t;

typedef struct {
    int bus;              // 0..32
    int speed_hz;         // enum {100000,400000,1000000}
    bool speed_set;
    size_t devices_count; // >=1
    i2c_device_t *devices;
} i2c_params_t;

typedef struct {
    i2c_params_t params;
} i2c_connector_t;


/* =========================
 * LoRaWAN gateway-bridge
 * =========================
 * bridge_url: "mqtt://..." ou "mqtts://..."
 * uplink_topic, downlink_topic: non vides
 * frequency_plan: enum
 * gateway_id: [1..64], ^[A-Za-z0-9_-]+$
 */
typedef enum { LORAWAN_EU868, LORAWAN_US915, LORAWAN_AS923, LORAWAN_AU915, LORAWAN_CN470, LORAWAN_IN865, LORAWAN_KR920, LORAWAN_RU864 } lorawan_freq_plan_t;

typedef struct {
    char *bridge_url;    // ^(mqtts?://)\S+$
    char *uplink_topic;  // minLength 1
    char *downlink_topic;// minLength 1
    lorawan_freq_plan_t frequency_plan;
    char *gateway_id;    // 1..64, ^[A-Za-z0-9_-]+$
} lorawan_params_t;

typedef struct {
    lorawan_params_t params;
} lorawan_connector_t;


/* =========================
 * 1-Wire
 * =========================
 * Deux backends exclusifs:
 *  - sysfs: backend="sysfs", sysfs_path, devices[{id,name?}], poll_ms [200..60000] (def 1000)
 *  - owfs : backend="owfs", host, port [1..65535](def 4304), devices[strings], poll_ms idem
 * ID regex: ^(10|22|26|28|3B|42|5[0-9A-Fa-f])(-?)[0-9A-Fa-f]{12}$
 */
typedef enum { ONEWIRE_SYSFS, ONEWIRE_OWFS } onewire_backend_t;

typedef struct {
    char *id;   // pattern
    char *name; // optional
} onewire_sysfs_device_t;

typedef struct {
    onewire_backend_t backend; // SYSFS
    char *sysfs_path;          // ^/sys/.+
    size_t devices_count;      // >=1
    onewire_sysfs_device_t *devices;
    int poll_ms;               // 200..60000, def 1000
    bool poll_set;
} onewire_sysfs_params_t;

typedef struct {
    onewire_backend_t backend; // OWFS
    char *host;
    int port;                  // 1..65535, def 4304
    bool port_set;
    size_t devices_count;      // >=1
    char **devices;            // array of ID strings
    int poll_ms;               // 200..60000, def 1000
    bool poll_set;
} onewire_owfs_params_t;

typedef struct {
    onewire_backend_t backend;
    union {
        onewire_sysfs_params_t sysfs;
        onewire_owfs_params_t  owfs;
    } u;
} onewire_params_t;

typedef struct {
    onewire_params_t params;
} onewire_connector_t;


/* =========================
 * OPC UA client
 * =========================
 * endpoint_url: "opc.tcp://host[:port][/path]"
 * security_policy: enum (def "None")
 * mode: {"None","Sign","SignAndEncrypt"} (def "None")
 * Si security_policy != None => cert_file & key_file requis
 * subscriptions[]: { node_id, sampling_ms [10..60000] }
 */
typedef enum {
    OPCUA_SEC_NONE,
    OPCUA_SEC_Basic128Rsa15,
    OPCUA_SEC_Basic256,
    OPCUA_SEC_Basic256Sha256,
    OPCUA_SEC_Aes128_Sha256_RsaOaep,
    OPCUA_SEC_Aes256_Sha256_RsaPss
} opcua_security_policy_t;

typedef enum { OPCUA_MODE_NONE, OPCUA_MODE_SIGN, OPCUA_MODE_SIGN_AND_ENCRYPT } opcua_security_mode_t;

typedef struct {
    char *node_id;  // minLength 1
    int sampling_ms;// 10..60000
} opcua_subscription_t;

typedef struct {
    char *endpoint_url;              // ^opc\.tcp://...
    opcua_security_policy_t security_policy; // default NONE
    bool security_policy_set;
    opcua_security_mode_t mode;      // default NONE
    bool mode_set;
    char *cert_file;                 // required if policy!=NONE
    char *key_file;                  // idem
    char *trustlist_dir;             // optional
    size_t subscriptions_count;      // optional
    opcua_subscription_t *subscriptions;
} opcua_params_t;

typedef struct {
    opcua_params_t params;
} opcua_connector_t;


/* =========================
 * SocketCAN
 * =========================
 * ifname: "canN" ou "vcanN"
 * bitrate: [10k..1M], sample_point: [0.4..0.9], restart_ms: [0..60000]
 * filters[]: {id, mask} avec étendue 29 bits (0..536870911)
 */
typedef struct {
    uint32_t id;   // 0..536870911
    uint32_t mask; // 0..536870911
} socketcan_filter_t;

typedef struct {
    char *ifname;        // ^(can|vcan)\d+$
    int bitrate;         // optional
    bool bitrate_set;
    double sample_point; // optional
    bool sample_point_set;
    int restart_ms;      // optional
    bool restart_ms_set;
    size_t filters_count;// optional
    socketcan_filter_t *filters;
} socketcan_params_t;

typedef struct {
    socketcan_params_t params;
} socketcan_connector_t;


/* =========================
 * Zigbee coordinator
 * =========================
 * serial.port: ^/dev/(ttyUSB|ttyACM|ttyAMA|serial)\d+$
 * serial.baudrate enum (def 115200), serial.adapter enum (def "auto")
 * channel: [11..26] (def 11)
 * pan_id: [0..65534], extended_pan_id: hex 16 chars
 * network_key: soit chaîne hex 32 chars, soit tableau de 16 octets
 * permit_join: bool (def false)
 * devices[]: {ieee hex 16, name?}
 */
typedef enum { ZIGBEE_ADAPTER_ZSTACK, 
    ZIGBEE_ADAPTER_EZSP, 
    ZIGBEE_ADAPTER_DECONZ, 
    ZIGBEE_ADAPTER_ZIGATE, 
    ZIGBEE_ADAPTER_CONBEE, 
    ZIGBEE_ADAPTER_AUTO } zigbee_adapter_t;

typedef struct {
    char *port;         // device path pattern
    int baudrate;       // {115200,230400,460800,1000000}, def 115200
    bool baudrate_set;
    zigbee_adapter_t adapter; // enum, def AUTO
    bool adapter_set;
} zigbee_serial_t;

    typedef struct {
        char *ieee; // 16 hex
        char *name; // optional
    } zigbee_device_alias_t;
    
typedef struct {
    zigbee_serial_t serial; // required
    int channel;            // 11..26, def 11
    bool channel_set;
    uint16_t pan_id;        // 0..65534
    char *extended_pan_id;  // ^[0-9A-Fa-f]{16}$ optional
    // network_key: support deux formats
    char *network_key_hex;  // 32 hex chars (option 1)
    uint8_t network_key_bytes[16]; // option 2
    bool has_network_key_hex;
    bool has_network_key_bytes;
    bool permit_join;       // def false
    bool permit_join_set;
    // optional known devices

    size_t devices_count;
    zigbee_device_alias_t *devices;
} zigbee_params_t;

typedef struct {
    zigbee_params_t params;
} zigbee_connector_t;

/* =========================
 * UART / Serial connector
 * =========================
 * port: /dev/ttyS… /dev/ttyUSB… /dev/ttyAMA… /dev/ttyACM… /dev/serial…
 * baudrate: {1200,2400,4800,9600,19200,38400,57600,115200,230400,460800,921600}
 * bytesize: {5,6,7,8}, def 8
 * parity: {"N","E","O","M","S"}, def "N"
 * stopbits: {1,1.5,2}, def 1
 * rtscts: bool, def false
 * xonxoff: bool, def false
 * timeout_ms: [0..60000], def 1000
 * packet: optional { start hex, end hex, length [1..2048] }
 */
typedef struct {
    char *start;   // hex string (e.g. "0x7E"), optional
    char *end;     // hex string (e.g. "0x7F"), optional
    int length;    // optional, 1..2048
    bool length_set;
} uart_packet_t;

typedef struct {
    char *port;      // device path pattern
    int baudrate;    // required enum
    int bytesize;    // 5,6,7,8 (default 8)
    bool bytesize_set;
    char parity;     // 'N','E','O','M','S' (default 'N')
    bool parity_set;
    double stopbits; // 1,1.5,2 (default 1)
    bool stopbits_set;
    bool rtscts;     // default false
    bool rtscts_set;
    bool xonxoff;    // default false
    bool xonxoff_set;
    int timeout_ms;  // 0..60000, default 1000
    bool timeout_set;
    bool has_packet;
    uart_packet_t packet;
} uart_params_t;

typedef struct {
    uart_params_t params;
} uart_connector_t;


#endif /* CONNECTORS_H */
