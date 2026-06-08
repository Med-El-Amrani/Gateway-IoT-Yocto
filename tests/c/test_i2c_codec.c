#include <assert.h>
#include <math.h>
#include <string.h>
#include "i2c_codec.h"

int main(void) {
    double value;
    uint8_t be[] = {0x01, 0x50};
    assert(i2c_decode_value(I2C_TYPE_S16, I2C_BE, be, 2,
                            0.0625, 1, &value) == 0);
    assert(fabs(value - 21.0) < 0.000001);
    uint8_t le[] = {0x78, 0x56, 0x34, 0x12};
    assert(i2c_decode_value(I2C_TYPE_U32, I2C_LE, le, 4,
                            1.0, 0, &value) == 0);
    assert(value == 305419896.0);
    char json[256];
    assert(i2c_format_json(json, sizeof(json), "sensor", 72, 0,
                           I2C_TYPE_BYTES, I2C_BE, be, 2, 1.0, 0) > 0);
    assert(strcmp(json,
        "{\"device\":\"sensor\",\"address\":72,\"register\":0,\"value\":\"0150\"}") == 0);
    return 0;
}
