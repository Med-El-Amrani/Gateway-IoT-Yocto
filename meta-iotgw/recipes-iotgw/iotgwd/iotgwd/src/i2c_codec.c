#include "i2c_codec.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t unsigned_bytes(const uint8_t *data, size_t length,
                               i2c_endianness_t endianness) {
    uint32_t value = 0;
    if (endianness == I2C_BE)
        for (size_t i = 0; i < length; ++i) value = (value << 8) | data[i];
    else
        for (size_t i = length; i > 0; --i) value = (value << 8) | data[i - 1];
    return value;
}

static int escape_name(const char *input, char *output, size_t size) {
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)input; *p; ++p) {
        if (*p < 0x20) return -1;
        if (*p == '"' || *p == '\\') {
            if (used + 2 >= size) return -1;
            output[used++] = '\\';
        } else if (used + 1 >= size) return -1;
        output[used++] = (char)*p;
    }
    output[used] = '\0';
    return 0;
}

int i2c_decode_value(i2c_point_type_t type, i2c_endianness_t endianness,
                     const uint8_t *data, size_t length,
                     double scale, int has_scale, double *value) {
    if (!data || !value || type == I2C_TYPE_BYTES) return -1;
    size_t required = (type == I2C_TYPE_U8 || type == I2C_TYPE_S8) ? 1 :
                      (type == I2C_TYPE_U16 || type == I2C_TYPE_S16) ? 2 :
                      type == I2C_TYPE_U24 ? 3 : 4;
    if (length != required) return -1;
    uint32_t raw = unsigned_bytes(data, length, endianness);
    double decoded;
    switch (type) {
    case I2C_TYPE_U8: decoded = (uint8_t)raw; break;
    case I2C_TYPE_S8: decoded = (int8_t)raw; break;
    case I2C_TYPE_U16: decoded = (uint16_t)raw; break;
    case I2C_TYPE_S16: decoded = (int16_t)raw; break;
    case I2C_TYPE_U24: decoded = raw; break;
    case I2C_TYPE_U32: decoded = raw; break;
    case I2C_TYPE_S32: decoded = (int32_t)raw; break;
    case I2C_TYPE_FLOAT: {
        float number;
        memcpy(&number, &raw, sizeof(number));
        decoded = number;
        break;
    }
    default: return -1;
    }
    decoded = has_scale ? decoded * scale : decoded;
    if (!isfinite(decoded)) return -1;
    *value = decoded;
    return 0;
}

int i2c_format_json(char *output, size_t output_size, const char *device,
                    unsigned int address, unsigned int reg,
                    i2c_point_type_t type, i2c_endianness_t endianness,
                    const uint8_t *data, size_t length,
                    double scale, int has_scale) {
    if (!output || !output_size || !device || !data) return -1;
    char safe_device[128];
    if (escape_name(device, safe_device, sizeof(safe_device)) != 0) return -1;
    int written;
    if (type == I2C_TYPE_BYTES) {
        char hex[33];
        if (length > 16) return -1;
        for (size_t i = 0; i < length; ++i)
            snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02x", data[i]);
        hex[length * 2] = '\0';
        written = snprintf(output, output_size,
            "{\"device\":\"%s\",\"address\":%u,\"register\":%u,\"value\":\"%s\"}",
            safe_device, address, reg, hex);
    } else {
        double value;
        if (i2c_decode_value(type, endianness, data, length,
                             scale, has_scale, &value) != 0) return -1;
        written = snprintf(output, output_size,
            "{\"device\":\"%s\",\"address\":%u,\"register\":%u,\"value\":%.10g}",
            safe_device, address, reg, value);
    }
    return written < 0 || (size_t)written >= output_size ? -1 : written;
}
