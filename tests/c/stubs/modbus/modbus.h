#pragma once
#include <stdint.h>
typedef struct _modbus modbus_t;
modbus_t *modbus_new_rtu(const char *, int, char, int, int);
modbus_t *modbus_new_tcp(const char *, int);
int modbus_set_slave(modbus_t *, int);
int modbus_set_response_timeout(modbus_t *, uint32_t, uint32_t);
int modbus_connect(modbus_t *);
void modbus_close(modbus_t *);
void modbus_free(modbus_t *);
int modbus_read_registers(modbus_t *, int, int, uint16_t *);
int modbus_read_input_registers(modbus_t *, int, int, uint16_t *);
int modbus_read_bits(modbus_t *, int, int, uint8_t *);
int modbus_read_input_bits(modbus_t *, int, int, uint8_t *);
