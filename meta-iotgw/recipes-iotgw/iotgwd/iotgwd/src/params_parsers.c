// ../src/params_parsers.c
#include <string.h>
#include <stdlib.h>
#include <yaml.h>
#include <regex.h>
#include "connectors.h"
#include "params_parsers.h"

static char* xstrdup(const char* s){
    if(!s) return NULL;
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
#define strdup xstrdup

static int match_regex(const char *pattern, const char *text) {
    regex_t regex;
    if (regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB) != 0)
        return 0; // compile failed -> treat as no match

    int rc = regexec(&regex, text, 0, NULL, 0);
    regfree(&regex);
    return rc == 0;
}

static yaml_node_t* ymap_get(yaml_document_t* doc, yaml_node_t* map, const char* key){
    if(!map || map->type != YAML_MAPPING_NODE) return NULL;
    for(yaml_node_pair_t* p = map->data.mapping.pairs.start; p < map->data.mapping.pairs.top; ++p){
        yaml_node_t* k = yaml_document_get_node(doc, p->key);
        yaml_node_t* v = yaml_document_get_node(doc, p->value);
        if(k && k->type == YAML_SCALAR_NODE && k->data.scalar.value &&
           strcmp((char*)k->data.scalar.value, key)==0)
            return v;
    }
    return NULL;
}
static const char* yscalar_str(yaml_node_t* n){ return (n && n->type==YAML_SCALAR_NODE) ? (const char*)n->data.scalar.value : NULL; }
static long yscalar_int(yaml_node_t* n, int* ok){ if(!n||n->type!=YAML_SCALAR_NODE){if(ok)*ok=0;return 0;} char* e=NULL; long v=strtol((char*)n->data.scalar.value,&e,10); if(ok)*ok=(e&&*e=='\0'); return v; }

int parse_http_server_params(yaml_document_t* doc, yaml_node_t* params, http_server_connector_t* out){
    memset(out, 0, sizeof(*out));
    if(!params || params->type!=YAML_MAPPING_NODE) return 0;
    const char* s;

    s = yscalar_str( ymap_get(doc, params, "bind") );
    if(s) out->params.bind = strdup(s);

    yaml_node_t* ba = ymap_get(doc, params, "basic_auth");
    if(ba && ba->type==YAML_MAPPING_NODE){
        const char* u = yscalar_str( ymap_get(doc, ba, "user") );
        const char* p = yscalar_str( ymap_get(doc, ba, "pass") );
        if(u || p){
            out->params.basic_auth.present = true;
            out->params.basic_auth.user = u? strdup(u): NULL;
            out->params.basic_auth.pass = p? strdup(p): NULL;
        }
    }
    yaml_node_t* routes = ymap_get(doc, params, "routes");
    if(routes && routes->type==YAML_SEQUENCE_NODE){
        size_t nitems = (routes->data.sequence.items.top - routes->data.sequence.items.start);
        out->params.routes = nitems? calloc(nitems, sizeof(http_route_t)) : NULL;
        out->params.routes_count = 0;
        for(yaml_node_item_t* it = routes->data.sequence.items.start; it < routes->data.sequence.items.top; ++it){
            yaml_node_t* rmap = yaml_document_get_node(doc, *it);
            if(!rmap || rmap->type!=YAML_MAPPING_NODE) continue;
            http_route_t *rt = &out->params.routes[out->params.routes_count++];
            const char* path = yscalar_str( ymap_get(doc, rmap, "path") );
            const char* method= yscalar_str( ymap_get(doc, rmap, "method") );
            rt->path = path? strdup(path): NULL;
            rt->method = HTTP_METHOD_GET;
            if(method){
                if(!strcmp(method,"POST")) rt->method=HTTP_METHOD_POST;
                else if(!strcmp(method,"PUT")) rt->method=HTTP_METHOD_PUT;
                else if(!strcmp(method,"PATCH")) rt->method=HTTP_METHOD_PATCH;
                else if(!strcmp(method,"DELETE")) rt->method=HTTP_METHOD_DELETE;
                else if(!strcmp(method,"HEAD")) rt->method=HTTP_METHOD_HEAD;
                else if(!strcmp(method,"OPTIONS")) rt->method=HTTP_METHOD_OPTIONS;
            }
        }
    }
    return 0;
}

int parse_mqtt_params(yaml_document_t* doc, yaml_node_t* params, mqtt_connector_t* out){
    memset(out, 0, sizeof(*out));
    if(!params || params->type!=YAML_MAPPING_NODE) return 0;
    const char* s; int ok=0; long v;

    s = yscalar_str( ymap_get(doc, params, "url") ); if(s) out->params.url = strdup(s);
    s = yscalar_str( ymap_get(doc, params, "host") ); if(s) out->params.host = strdup(s);
    v = yscalar_int( ymap_get(doc, params, "port"), &ok ); if(ok) out->params.port = (int)v;
    s = yscalar_str( ymap_get(doc, params, "client_id") ); if(s) out->params.client_id = strdup(s);

    v = yscalar_int( ymap_get(doc, params, "keepalive_s"), &ok ); if(ok){ out->params.keepalive_s=(int)v; out->params.keepalive_set=true; }
    v = yscalar_int( ymap_get(doc, params, "qos"), &ok ); if(ok){ out->params.qos=(int)v; out->params.qos_set=true; }

    s = yscalar_str( ymap_get(doc, params, "clean_session") ); if(s){ out->params.clean_session_set=true; out->params.clean_session = (!strcmp(s,"true")||!strcmp(s,"1")); }
    s = yscalar_str( ymap_get(doc, params, "retain") ); if(s){ out->params.retain_set=true; out->params.retain = (!strcmp(s,"true")||!strcmp(s,"1")); }
    s = yscalar_str( ymap_get(doc, params, "username") ); if(s) out->params.username = strdup(s);
    s = yscalar_str( ymap_get(doc, params, "password") ); if(s) out->params.password = strdup(s);

    yaml_node_t* tls = ymap_get(doc, params, "tls");
    if(tls && tls->type==YAML_MAPPING_NODE){
        out->params.tls.present = true;
        s = yscalar_str( ymap_get(doc, tls, "ca_file") ); if(s) out->params.tls.ca_file = strdup(s);
        s = yscalar_str( ymap_get(doc, tls, "cert_file") ); if(s) out->params.tls.cert_file = strdup(s);
        s = yscalar_str( ymap_get(doc, tls, "key_file") ); if(s) out->params.tls.key_file = strdup(s);
        s = yscalar_str( ymap_get(doc, tls, "enabled") ); if(s) out->params.tls.enabled = (!strcmp(s,"true")||!strcmp(s,"1"));
        s = yscalar_str( ymap_get(doc, tls, "insecure_skip_verify") ); if(s) out->params.tls.insecure_skip_verify = (!strcmp(s,"true")||!strcmp(s,"1"));
    }

    yaml_node_t* topics = ymap_get(doc, params, "topics");
    if(topics && topics->type==YAML_SEQUENCE_NODE){
        size_t nitems = (topics->data.sequence.items.top - topics->data.sequence.items.start);
        out->params.topics = nitems? calloc(nitems, sizeof(mqtt_topic_t)) : NULL;
        out->params.topics_count = 0;
        for(yaml_node_item_t* it = topics->data.sequence.items.start; it < topics->data.sequence.items.top; ++it){
            yaml_node_t* tmap = yaml_document_get_node(doc, *it);
            if(!tmap || tmap->type!=YAML_MAPPING_NODE) continue;
            mqtt_topic_t* tp = &out->params.topics[out->params.topics_count++];
            const char* tt = yscalar_str( ymap_get(doc, tmap, "topic") ); if(tt) tp->topic = strdup(tt);
            int ok2=0; long qos = yscalar_int( ymap_get(doc, tmap, "qos"), &ok2 ); if(ok2){ tp->qos = (int)qos; tp->qos_set=true; }
        }
    }
    return 0;
}

int parse_modbus_rtu_params(yaml_document_t* doc, yaml_node_t* params, modbus_rtu_connector_t* out){
    memset(out, 0, sizeof(*out));
    if(!params || params->type!=YAML_MAPPING_NODE) return 0;

    const char* s; int ok=0; long v;
    s = yscalar_str( ymap_get(doc, params, "port") ); if(s) out->params.port = strdup(s);
    v = yscalar_int( ymap_get(doc, params, "baudrate"), &ok ); if(ok) out->params.baudrate=(int)v;
    s = yscalar_str( ymap_get(doc, params, "parity") ); if(s) out->params.parity = s[0];
    v = yscalar_int( ymap_get(doc, params, "stopbits"), &ok ); if(ok) out->params.stopbits=(int)v;
    v = yscalar_int( ymap_get(doc, params, "timeout_ms"), &ok ); if(ok) out->params.timeout_ms=(int)v;

    yaml_node_t* rs = ymap_get(doc, params, "rs485");
    if(rs && rs->type==YAML_MAPPING_NODE){
        out->params.rs485.present = true;
        int ok1=0; long a = yscalar_int( ymap_get(doc, rs, "rts_time_before_ms"), &ok1 );
        if(ok1) out->params.rs485.rts_time_before_ms = (int)a;
        ok1=0; long b = yscalar_int( ymap_get(doc, rs, "rts_time_after_ms"), &ok1 );
        if(ok1) out->params.rs485.rts_time_after_ms = (int)b;
    }

    yaml_node_t* slaves = ymap_get(doc, params, "slaves");
    if(slaves && slaves->type==YAML_SEQUENCE_NODE){
        size_t nitems = (slaves->data.sequence.items.top - slaves->data.sequence.items.start);
        out->params.slaves = nitems? calloc(nitems, sizeof(modbus_slave_t)) : NULL;
        out->params.slaves_count = 0;
        for(yaml_node_item_t* it = slaves->data.sequence.items.start; it < slaves->data.sequence.items.top; ++it){
            yaml_node_t* smap = yaml_document_get_node(doc, *it);
            if(!smap || smap->type!=YAML_MAPPING_NODE) continue;
            modbus_slave_t* sl = &out->params.slaves[out->params.slaves_count++];
            int ok2=0; long uid = yscalar_int( ymap_get(doc, smap, "unit_id"), &ok2 ); if(ok2) sl->unit_id=(uint8_t)uid;
            ok2=0; long poll = yscalar_int( ymap_get(doc, smap, "poll_ms"), &ok2 ); if(ok2) sl->poll_ms=(uint32_t)poll;

            yaml_node_t* map = ymap_get(doc, smap, "map");
            if(map && map->type==YAML_SEQUENCE_NODE){
                size_t mitems = (map->data.sequence.items.top - map->data.sequence.items.start);
                sl->map = mitems? calloc(mitems, sizeof(modbus_point_t)) : NULL;
                sl->map_count = 0;
                for(yaml_node_item_t* it2 = map->data.sequence.items.start; it2 < map->data.sequence.items.top; ++it2){
                    yaml_node_t* pmap = yaml_document_get_node(doc, *it2);
                    if(!pmap || pmap->type!=YAML_MAPPING_NODE) continue;
                    modbus_point_t* pt = &sl->map[sl->map_count++];
                    const char* nm = yscalar_str( ymap_get(doc, pmap, "name") ); if(nm) pt->name = strdup(nm);
                    const char* fn = yscalar_str( ymap_get(doc, pmap, "func") );
                    if(fn){
                        if(!strcmp(fn,"holding")) pt->func=MODBUS_FUNC_HOLDING;
                        else if(!strcmp(fn,"input")) pt->func=MODBUS_FUNC_INPUT;
                        else if(!strcmp(fn,"coil")) pt->func=MODBUS_FUNC_COIL;
                        else pt->func=MODBUS_FUNC_DISCRETE;
                    }
                    int ok3=0; long addr = yscalar_int( ymap_get(doc, pmap, "addr"), &ok3 ); if(ok3) pt->addr=(uint16_t)addr;
                    ok3=0; long cnt  = yscalar_int( ymap_get(doc, pmap, "count"), &ok3 ); if(ok3) pt->count=(uint8_t)cnt;
                    const char* ty = yscalar_str( ymap_get(doc, pmap, "type") );
                    if(ty){
                        if(!strcmp(ty,"u16")) pt->type=MODBUS_TYPE_U16;
                        else if(!strcmp(ty,"s16")) pt->type=MODBUS_TYPE_S16;
                        else if(!strcmp(ty,"u32")) pt->type=MODBUS_TYPE_U32;
                        else if(!strcmp(ty,"s32")) pt->type=MODBUS_TYPE_S32;
                        else if(!strcmp(ty,"float")) pt->type=MODBUS_TYPE_FLOAT;
                        else pt->type=MODBUS_TYPE_DOUBLE;
                    }
                    const char* sc = yscalar_str( ymap_get(doc, pmap, "scale") );
                    if(sc){ pt->scale = atof(sc); pt->has_scale=true; }
                    const char* sg = yscalar_str( ymap_get(doc, pmap, "signed") );
                    if(sg){ pt->signed_flag = (!strcmp(sg,"true")||!strcmp(sg,"1")); pt->has_signed=true; }
                }
            }
        }
    }
    return 0;
}

int parse_modbus_tcp_params(yaml_document_t* doc, yaml_node_t* params, modbus_tcp_connector_t* out){
    memset(out, 0, sizeof(*out));
    if(!params || params->type != YAML_MAPPING_NODE) return 0;

    const char* s; int ok=0; long v;

    s = yscalar_str( ymap_get(doc, params, "host") );
    if(s) out->params.host = strdup(s);

    v = yscalar_int( ymap_get(doc, params, "port"), &ok );
    if(ok){ out->params.port = (uint16_t)v; out->params.port_set = true; }

    v = yscalar_int( ymap_get(doc, params, "unit_id"), &ok );
    if(ok){ out->params.unit_id = (uint8_t)v; out->params.unit_id_set = true; }

    v = yscalar_int( ymap_get(doc, params, "timeout_ms"), &ok );
    if(ok){ out->params.timeout_ms = (int)v; }

    v = yscalar_int( ymap_get(doc, params, "retries"), &ok );
    if(ok){ out->params.retries = (int)v; out->params.retries_set = true; }

    yaml_node_t* map = ymap_get(doc, params, "map");
    if(map && map->type == YAML_SEQUENCE_NODE){
        size_t mitems = (map->data.sequence.items.top - map->data.sequence.items.start);
        out->params.map = mitems ? calloc(mitems, sizeof(modbus_tcp_point_t)) : NULL;
        out->params.map_count = 0;

        for(yaml_node_item_t* it = map->data.sequence.items.start; it < map->data.sequence.items.top; ++it){
            yaml_node_t* pmap = yaml_document_get_node(doc, *it);
            if(!pmap || pmap->type!=YAML_MAPPING_NODE) continue;

            modbus_tcp_point_t* pt = &out->params.map[out->params.map_count++];

            const char* nm = yscalar_str( ymap_get(doc, pmap, "name") ); if(nm) pt->name = strdup(nm);

            const char* fn = yscalar_str( ymap_get(doc, pmap, "func") );
            if(fn){
                if(!strcmp(fn,"holding")) pt->func=MODBUS_FUNC_HOLDING;
                else if(!strcmp(fn,"input")) pt->func=MODBUS_FUNC_INPUT;
                else if(!strcmp(fn,"coil")) pt->func=MODBUS_FUNC_COIL;
                else pt->func=MODBUS_FUNC_DISCRETE;
            }

            int ok2=0; long addr = yscalar_int( ymap_get(doc, pmap, "addr"), &ok2 ); if(ok2) pt->addr=(uint16_t)addr;
            ok2=0; long cnt  = yscalar_int( ymap_get(doc, pmap, "count"), &ok2 ); if(ok2) pt->count=(uint8_t)cnt;

            const char* ty = yscalar_str( ymap_get(doc, pmap, "type") );
            if(ty){
                if(!strcmp(ty,"u16")) pt->type=MODBUS_TYPE_U16;
                else if(!strcmp(ty,"s16")) pt->type=MODBUS_TYPE_S16;
                else if(!strcmp(ty,"u32")) pt->type=MODBUS_TYPE_U32;
                else if(!strcmp(ty,"s32")) pt->type=MODBUS_TYPE_S32;
                else if(!strcmp(ty,"float")) pt->type=MODBUS_TYPE_FLOAT;
                else pt->type=MODBUS_TYPE_DOUBLE;
            }

            const char* sc = yscalar_str( ymap_get(doc, pmap, "scale") );
            if(sc){ pt->scale = atof(sc); pt->has_scale=true; }

            const char* sg = yscalar_str( ymap_get(doc, pmap, "signed") );
            if(sg){ pt->signed_flag = (!strcmp(sg,"true")||!strcmp(sg,"1")); pt->has_signed=true; }
        }
    }
    return 0;
}

int parse_uart_params(yaml_document_t* doc, yaml_node_t* params, uart_connector_t* out){
    memset(out, 0, sizeof(*out));
    if(!params || params->type!=YAML_MAPPING_NODE) return 0;
    const char* s; int ok=0; long v;
    s = yscalar_str( ymap_get(doc, params, "port") ); if(s) out->params.port = strdup(s);
    v = yscalar_int( ymap_get(doc, params, "baudrate"), &ok ); if(ok) out->params.baudrate=(int)v;
    v = yscalar_int( ymap_get(doc, params, "bytesize"), &ok ); if(ok){ out->params.bytesize=(int)v; out->params.bytesize_set=true; }
    s = yscalar_str( ymap_get(doc, params, "parity") ); if(s){ out->params.parity=s[0]; out->params.parity_set=true; }
    s = yscalar_str( ymap_get(doc, params, "stopbits") ); if(s){ out->params.stopbits=atof(s); out->params.stopbits_set=true; }
    s = yscalar_str( ymap_get(doc, params, "rtscts") ); if(s){ out->params.rtscts=(!strcmp(s,"true")||!strcmp(s,"1")); out->params.rtscts_set=true; }
    s = yscalar_str( ymap_get(doc, params, "xonxoff") ); if(s){ out->params.xonxoff=(!strcmp(s,"true")||!strcmp(s,"1")); out->params.xonxoff_set=true; }
    int ok2=0; long to = yscalar_int( ymap_get(doc, params, "timeout_ms"), &ok2 ); if(ok2){ out->params.timeout_ms=(int)to; out->params.timeout_set=true; }

    yaml_node_t* pk = ymap_get(doc, params, "packet");
    if(pk && pk->type==YAML_MAPPING_NODE){
        out->params.has_packet = true;
        const char* st = yscalar_str( ymap_get(doc, pk, "start") ); if(st) out->params.packet.start = strdup(st);
        const char* en = yscalar_str( ymap_get(doc, pk, "end") );   if(en) out->params.packet.end   = strdup(en);
        int ok3=0; long ln = yscalar_int( ymap_get(doc, pk, "length"), &ok3 ); if(ok3){ out->params.packet.length=(int)ln; out->params.packet.length_set=true; }
    }
    return 0;
}


int parse_spi_params(yaml_document_t* doc, yaml_node_t* params, spi_connector_t* out){
    memset(out, 0, sizeof(*out));
    if(!params || params->type!= YAML_MAPPING_NODE) return 0;
    const char* s; int ok=0;  long v;

    s = yscalar_str(ymap_get(doc, params,"device")); 
    if(s) out->params.device = strdup(s);

    v=yscalar_int(ymap_get(doc,params,"mode"),&ok);
    if(ok){
        out->params.mode=(int)v;
        out->params.mode_set=true;
    }

    v=yscalar_int(ymap_get(doc, params, "bits_per_word"),&ok);
    if(ok){out->params.bits_per_word=(int)v; out->params.bpw_set=true;}

    v=yscalar_int(ymap_get(doc, params,"speed_hz"),&ok);
    if(ok){out->params.speed_hz=(int)v; out->params.speed_set=true;}

    s=yscalar_str(ymap_get(doc, params, "lsb_first"));
    if(s){
        out->params.lsb_first=(!(strcmp(s,"true"))|| !(strcmp(s,"1")));
        out->params.lsb_first_set = true;
    }

    s=yscalar_str(ymap_get(doc,params, "cs_change"));
    if(s){
        out->params.cs_change=(!(strcmp(s,"true")) || !(strcmp(s,"1")));
        out->params.cs_change_set=true;
    }

    yaml_node_t* transactions=ymap_get(doc, params, "transactions");
    if(transactions && transactions->type==YAML_SEQUENCE_NODE){
        size_t n_items=(transactions->data.sequence.items.top - transactions->data.sequence.items.start);
        out->params.transactions= n_items ? calloc(n_items, sizeof(spi_transaction_t)):NULL;
        out->params.transactions_count=0;
        for(yaml_node_item_t* item=transactions->data.sequence.items.start;item < transactions->data.sequence.items.top; ++item){
            yaml_node_t* item_node=yaml_document_get_node(doc,*item);

            if(!item_node || item_node->type!=YAML_MAPPING_NODE) continue;

            spi_transaction_t* tr=&out->params.transactions[out->params.transactions_count++];

            s=yscalar_str(ymap_get(doc, item_node, "op"));
            if(s){
                if(strcmp(s,"read")==0) tr->op=SPI_OP_READ;
                else if(strcmp(s,"write")==0) tr->op=SPI_OP_WRITE;
                else tr->op=SPI_OP_TRANSFER;
            }

            v=yscalar_int(ymap_get(doc,item_node,"len"),&ok);
            if(ok){
                if(v>=1 && v<=4096) tr->len=(int)v;
            }

            s=yscalar_str(ymap_get(doc, item_node, "tx"));
            if (s) {
                // Check with regex before accepting
                if (match_regex("^(0x)?[0-9A-Fa-f]+$", s)) {
                    tr->tx = strdup(s);
                    tr->has_tx = true;
                } else {
                    fprintf(stderr, "WARN: invalid tx hex string: %s\n", s);
                }
            }
            
            v=yscalar_int(ymap_get(doc,item_node,"rx_len"),&ok);
             if(ok){
                if(v>=1 && v<=4096){ tr->rx_len=(int)v;
                    tr->has_rx_len=true;
                }
            }
        }
    }
    
    return 0;
}

