#include <assert.h>
#include <math.h>
#include <string.h>

#include "modbus_codec.h"

int main(void) {
    double value = 0;
    uint16_t s16[] = {0xff9c};
    assert(modbus_decode_value(MODBUS_TYPE_S16, s16, 1, 0.1, 1, &value) == 0);
    assert(fabs(value - (-10.0)) < 0.000001);

    uint16_t u32[] = {0x1234, 0x5678};
    assert(modbus_decode_value(MODBUS_TYPE_U32, u32, 2, 1.0, 0, &value) == 0);
    assert(value == 305419896.0);

    uint16_t floating[] = {0x4148, 0x0000};
    assert(modbus_decode_value(MODBUS_TYPE_FLOAT, floating, 2, 1.0, 0, &value) == 0);
    assert(fabs(value - 12.5) < 0.000001);

    uint16_t precise[] = {0x3ff8, 0x0000, 0x0000, 0x0000};
    assert(modbus_decode_value(MODBUS_TYPE_DOUBLE, precise, 4, 1.0, 0, &value) == 0);
    assert(fabs(value - 1.5) < 0.000001);
    assert(modbus_decode_value(MODBUS_TYPE_DOUBLE, precise, 2, 1.0, 0, &value) == -1);

    char json[128];
    assert(modbus_format_json(json, sizeof(json), "temperature", 7, 21.5) > 0);
    assert(strcmp(json, "{\"name\":\"temperature\",\"unit_id\":7,\"value\":21.5}") == 0);
    return 0;
}
