#pragma once

#include <stddef.h>
#include <stdint.h>
#include "connectors.h"

int modbus_decode_value(modbus_datatype_t type, const uint16_t *registers,
                        size_t count, double scale, int has_scale,
                        double *value);
int modbus_format_json(char *output, size_t output_size, const char *name,
                       int unit_id, double value);
