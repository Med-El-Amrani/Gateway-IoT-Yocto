#include "runtime.h"
#include <string.h>

void runtime_reset(runtime_cfg_t *rt){
    memset(rt, 0, sizeof(*rt));
}
