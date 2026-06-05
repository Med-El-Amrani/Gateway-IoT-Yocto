#include "modbus_codec.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

int modbus_decode_value(modbus_datatype_t type, const uint16_t *r,
                        size_t count, double scale, int has_scale,
                        double *value) {
    if (!r || !value) return -1;
    double decoded;
    switch (type) {
    case MODBUS_TYPE_U16:
        if (count < 1) return -1;
        decoded = r[0];
        break;
    case MODBUS_TYPE_S16:
        if (count < 1) return -1;
        decoded = (int16_t)r[0];
        break;
    case MODBUS_TYPE_U32:
    case MODBUS_TYPE_S32:
    case MODBUS_TYPE_FLOAT: {
        if (count < 2) return -1;
        uint32_t bits = ((uint32_t)r[0] << 16) | r[1];
        if (type == MODBUS_TYPE_U32) decoded = bits;
        else if (type == MODBUS_TYPE_S32) decoded = (int32_t)bits;
        else {
            float number;
            memcpy(&number, &bits, sizeof(number));
            decoded = number;
        }
        break;
    }
    case MODBUS_TYPE_DOUBLE: {
        if (count < 4) return -1;
        uint64_t bits = ((uint64_t)r[0] << 48) | ((uint64_t)r[1] << 32) |
                        ((uint64_t)r[2] << 16) | r[3];
        double number;
        memcpy(&number, &bits, sizeof(number));
        decoded = number;
        break;
    }
    default:
        return -1;
    }
    decoded = has_scale ? decoded * scale : decoded;
    if (!isfinite(decoded)) return -1;
    *value = decoded;
    return 0;
}

int modbus_format_json(char *output, size_t output_size, const char *name,
                       int unit_id, double value) {
    if (!output || output_size == 0 || !name) return -1;
    int written = snprintf(output, output_size,
                           "{\"name\":\"%s\",\"unit_id\":%d,\"value\":%.10g}",
                           name, unit_id, value);
    return written < 0 || (size_t)written >= output_size ? -1 : written;
}
