#pragma once
#include <stddef.h>
#include <stdint.h>
#include "connectors.h"

int i2c_decode_value(i2c_point_type_t type, i2c_endianness_t endianness,
                     const uint8_t *data, size_t length,
                     double scale, int has_scale, double *value);
int i2c_format_json(char *output, size_t output_size, const char *device,
                    unsigned int address, unsigned int reg,
                    i2c_point_type_t type, i2c_endianness_t endianness,
                    const uint8_t *data, size_t length,
                    double scale, int has_scale);
