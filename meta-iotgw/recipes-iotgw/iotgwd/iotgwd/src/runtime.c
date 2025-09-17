// src/runtime.c
#include "runtime.h"
#include <string.h>

void runtime_reset(runtime_cfg_t *rt){
    if(!rt) return;
    memset(rt, 0, sizeof(*rt));
}

connector_t *runtime_add_connector(runtime_cfg_t *rt, const connector_t *src){
    if(!rt || !src) return NULL;
    if(rt->n_connectors >= IOTGW_MAX_CONNECTORS) return NULL;

    connector_t *dst = &rt->connectors[rt->n_connectors++];
    *dst = *src; // copie shallow (pointeurs conservés)
    return dst;
}

connector_t *runtime_find_connector(runtime_cfg_t *rt, const char *id){
    if(!rt || !id) return NULL;
    for(size_t i = 0; i < rt->n_connectors; ++i){
        const char *cid = rt->connectors[i].id;
        if(cid && strcmp(cid, id) == 0){
            return &rt->connectors[i];
        }
    }
    return NULL;
}

bridge_t *runtime_add_bridge(runtime_cfg_t *rt, const bridge_t *src){
    if(!rt || !src) return NULL;
    if(rt->n_bridges >= IOTGW_MAX_BRIDGES) return NULL;

    bridge_t *dst = &rt->bridges[rt->n_bridges++];
    *dst = *src; // copie shallow (id/pointeurs conservés)
    return dst;
}
