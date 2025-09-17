// src/config_loader.c
#include "config_loader.h"
#include "connector.h"
#include "bridge.h"
#include "runtime.h"
#include "fs.h"
#include "log.h"

// Connecteurs concrets
#include "conn_mqtt.h"
#include "conn_spi.h"

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

// -----------------------------------------------------------------------------
// Helpers parsing "YAML minimaliste"
// ATTENTION: ceci n'est PAS un parseur YAML général. Il supporte un format
// très simple, à indentation espaces, sans listes complexes, sans quotes.
// Exemple attendu:
//
// gateway:
//   name: "gw01"
// connectors:
//   mqtt_local:
//     type: mqtt
//     params:
//       url: mqtt://localhost:1883
//       client_id: gw01
//   spi0:
//     type: spi
//     params:
//       device: /dev/spidev0.0
//       mode: 0
//       speed_hz: 500000
//
// bridges:
//   b1:
//     from: mqtt_local
//     to:   spi0
//     mapping:
//       - key: sensor/temp
//         target: reg_10
//
// -----------------------------------------------------------------------------


typedef struct {
    char id[64];
    char type[32];
    // MQTT
    char url[256];
    char client_id[128];
    int  keepalive_s;

    // SPI
    char device[128];
    int  mode;
    int  speed_hz;
} pending_connector_t;

typedef struct {
    char id[64];
    char from[64];
    char to[64];
    // mapping minimal: une seule paire key/target pour la démo
    char key[128];
    char target[128];
} pending_bridge_t;

static void trim(char *s){
    // trim in place
    size_t n = strlen(s);
    while(n && (s[n-1]=='\r' || s[n-1]=='\n' || isspace((unsigned char)s[n-1]))) s[--n]=0;
    size_t i=0; while(s[i] && isspace((unsigned char)s[i])) i++;
    if(i) memmove(s, s+i, strlen(s+i)+1);
}

static int starts_with(const char *s, const char *pfx){
    return strncmp(s,pfx,strlen(pfx))==0;
}

static const char* after(const char* s, const char* pfx){
    size_t m=strlen(pfx);
    return starts_with(s,pfx)?(s+m):NULL;
}

static void unquote(char *v){
    size_t n=strlen(v);
    if(n>=2 && ((v[0]=='"' && v[n-1]=='"') || (v[0]=='\'' && v[n-1]=='\''))){
        v[n-1]=0; memmove(v,v+1,n-1);
    }
}

static int parse_kv(char *line, char *key, size_t ksz, char *val, size_t vsz){
    // attend "key: value" (value optionnel)
    // retourne 1 si ça match, 0 sinon
    char *colon = strchr(line, ':');
    if(!colon) return 0;
    *colon = 0;
    strncpy(key, line, ksz-1); key[ksz-1]=0; trim(key);
    char *p = colon+1;
    while(*p && isspace((unsigned char)*p)) p++;
    strncpy(val, p, vsz-1); val[vsz-1]=0; trim(val);
    unquote(val);
    return 1;
}


// -----------------------------------------------------------------------------
// Lecture d’un document YAML simple -> crée des connecteurs/bridges
// -----------------------------------------------------------------------------

static int apply_connectors(runtime_cfg_t *rt, pending_connector_t *pc, size_t n){
    for(size_t i=0;i<n;i++){
        pending_connector_t *c = &pc[i];

        if(strcmp(c->type,"mqtt")==0){
            conn_mqtt_cfg_t m = {
                .url = c->url[0] ? c->url : "mqtt://localhost:1883",
                .client_id = c->client_id[0] ? c->client_id : c->id,
                .keepalive_s = c->keepalive_s > 0 ? c->keepalive_s : 60,
            };
            connector_t *cc = conn_mqtt_create(c->id, &m);
            if(!cc){ log_err("MQTT create failed for id=%s", c->id); return -1; }
            if(runtime_add_connector(rt, cc) != 0){
                log_err("runtime_add_connector failed for id=%s", c->id);
                cc->destroy(cc);
                return -1;
            }
        }
        else if(strcmp(c->type,"spi")==0){
            conn_spi_cfg_t s = {
                .device   = c->device[0] ? c->device : "/dev/spidev0.0",
                .mode     = (unsigned)c->mode,
                .speed_hz = (unsigned)c->speed_hz > 0 ? (unsigned)c->speed_hz : 500000,
            };
            connector_t *cc = conn_spi_create(c->id, &s);
            if(!cc){ log_err("SPI create failed for id=%s", c->id); return -1; }
            if(runtime_add_connector(rt, cc) != 0){
                log_err("runtime_add_connector failed for id=%s", c->id);
                cc->destroy(cc);
                return -1;
            }
        }
        else {
            log_warn("Unsupported connector type: %s (id=%s)", c->type, c->id);
        }
    }
    return 0;
}

static int apply_bridges(runtime_cfg_t *rt, pending_bridge_t *pb, size_t n){
    for(size_t i=0;i<n;i++){
        bridge_rule_t rule = {0};
        // Règle très simple: une seule clé source -> clé cible
        // Dans un vrai système, tu auras un tableau de règles.
        rule.kind = BRIDGE_MAP_KEY_TO_KEY;
        strncpy(rule.src.key, pb[i].key, sizeof(rule.src.key)-1);
        strncpy(rule.dst.key, pb[i].target, sizeof(rule.dst.key)-1);

        bridge_t *b = bridge_create(pb[i].id, pb[i].from, pb[i].to, &rule, 1);
        if(!b){ log_err("bridge_create failed for id=%s", pb[i].id); return -1; }
        if(runtime_add_bridge(rt, b) != 0){
            log_err("runtime_add_bridge failed for id=%s", pb[i].id);
            bridge_destroy(b);
            return -1;
        }
    }
    return 0;
}


// Parse hyper simple du YAML
static int parse_yaml_minimal(runtime_cfg_t *rt, const char *path, const char *doc){
    (void)path;

    // On balaye le document ligne par ligne et on garde un petit état
    // pour savoir si on est dans "connectors", "bridges", et quel item est courant.
    char *buf = strdup(doc);
    if(!buf) return -1;

    pending_connector_t conns[64]; size_t nc=0;
    pending_bridge_t    brs[64];   size_t nb=0;

    char *saveptr=NULL;
    for(char *line = strtok_r(buf, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)){
        // On travaille sur une copie modifiable
        char l[512]; strncpy(l, line, sizeof(l)-1); l[sizeof(l)-1]=0;
        // ignorer commentaires
        char *hash = strchr(l, '#'); if(hash) *hash=0;
        trim(l);
        if(!*l) continue;

        // Contexte (niveaux par indentation simple - 2 espaces par niveau)
        int indent = 0; { const char *p=line; while(*p==' ') { indent++; p++; } }

        static int in_connectors = 0;
        static int in_bridges = 0;
        static int in_params  = 0;
        static int in_mapping = 0; // liste "mapping:"
        static char current_id[64]="";

        char key[128], val[384];

        if(indent == 0){
            // racine
            in_connectors = in_bridges = in_params = in_mapping = 0;
            current_id[0]=0;

            if(parse_kv(l, key, sizeof(key), val, sizeof(val))){
                if(strcmp(key,"connectors")==0){ in_connectors = 1; }
                else if(strcmp(key,"bridges")==0){ in_bridges = 1; }
            }
            continue;
        }

        if(in_connectors){
            if(indent == 2){
                // "<id>:"
                if(l[strlen(l)-1]==':'){
                    if(nc >= (sizeof(conns)/sizeof(conns[0]))){ log_err("Too many connectors"); free(buf); return -1; }
                    memset(&conns[nc], 0, sizeof(conns[nc]));
                    strncpy(conns[nc].id, l, sizeof(conns[nc].id)-1);
                    // retire le ':' final
                    conns[nc].id[strlen(conns[nc].id)-1]=0; trim(conns[nc].id);
                    strncpy(current_id, conns[nc].id, sizeof(current_id)-1);
                    continue;
                }
            } else if(indent == 4 && current_id[0]){
                if(parse_kv(l, key, sizeof(key), val, sizeof(val))){
                    if(strcmp(key,"type")==0){
                        strncpy(conns[nc].type, val, sizeof(conns[nc].type)-1);
                    } else if(strcmp(key,"params")==0){
                        in_params = 1;
                    }
                }
                continue;
            } else if(indent == 6 && in_params && current_id[0]){
                // params k/v
                if(parse_kv(l, key, sizeof(key), val, sizeof(val))){
                    pending_connector_t *c = &conns[nc];
                    if(strcmp(key,"url")==0)        { strncpy(c->url, val, sizeof(c->url)-1); }
                    else if(strcmp(key,"client_id")==0){ strncpy(c->client_id, val, sizeof(c->client_id)-1); }
                    else if(strcmp(key,"keepalive_s")==0){ c->keepalive_s = atoi(val); }
                    else if(strcmp(key,"device")==0) { strncpy(c->device, val, sizeof(c->device)-1); }
                    else if(strcmp(key,"mode")==0)   { c->mode = atoi(val); }
                    else if(strcmp(key,"speed_hz")==0){ c->speed_hz = atoi(val); }
                }
                continue;
            } else if(indent <= 2 && current_id[0]){
                // fin de cet item connector
                nc++;
                current_id[0]=0; in_params=0;
                // La ligne courante sera re-traitée au prochain tour (mais ici on continue)
            }
        }

        if(in_bridges){
            if(indent == 2){
                // "<id>:"
                if(l[strlen(l)-1]==':'){
                    if(nb >= (sizeof(brs)/sizeof(brs[0]))){ log_err("Too many bridges"); free(buf); return -1; }
                    memset(&brs[nb], 0, sizeof(brs[nb]));
                    strncpy(brs[nb].id, l, sizeof(brs[nb].id)-1);
                    brs[nb].id[strlen(brs[nb].id)-1]=0; trim(brs[nb].id);
                    strncpy(current_id, brs[nb].id, sizeof(current_id)-1);
                    continue;
                }
            } else if(indent == 4 && current_id[0]){
                if(parse_kv(l, key, sizeof(key), val, sizeof(val))){
                    if(strcmp(key,"from")==0) strncpy(brs[nb].from, val, sizeof(brs[nb].from)-1);
                    else if(strcmp(key,"to")==0) strncpy(brs[nb].to, val, sizeof(brs[nb].to)-1);
                    else if(strcmp(key,"mapping")==0) in_mapping = 1;
                }
                continue;
            } else if(indent == 6 && in_mapping && current_id[0]){
                // On attend des lignes commençant par "- key:" puis "target:"
                if(l[0]=='-' && (l[1]==' ' || l[1]=='\t')){
                    // "- key: value" (on lit seulement la première entrée)
                    char *p = l+2; trim(p);
                    if(parse_kv(p, key, sizeof(key), val, sizeof(val))){
                        if(strcmp(key,"key")==0) strncpy(brs[nb].key, val, sizeof(brs[nb].key)-1);
                    }
                } else if(parse_kv(l, key, sizeof(key), val, sizeof(val))){
                    if(strcmp(key,"target")==0) strncpy(brs[nb].target, val, sizeof(brs[nb].target)-1);
                }
                continue;
            } else if(indent <= 2 && current_id[0]){
                // fin de cet item bridge
                nb++;
                current_id[0]=0; in_mapping=0;
            }
        }
    }

    // fermer le dernier item si nécessaire
    if(conns[nc].id[0] && conns[nc].type[0]) nc++;
    if(brs[nb].id[0] && brs[nb].from[0]) nb++;

    free(buf);

    // Instancier
    if(apply_connectors(rt, conns, nc) != 0) return -1;
    if(apply_bridges(rt, brs, nb) != 0) return -1;

    return 0;
}


// -----------------------------------------------------------------------------
// API appelée par app.c
// -----------------------------------------------------------------------------

static int load_one(runtime_cfg_t *rt, const char *path){
    char buf[128*1024];
    int n = fs_read_file(path, buf, sizeof(buf));
    if(n < 0){
        log_err("Cannot read config: %s (err=%d)", path, n);
        return -1;
    }
    if(parse_yaml_minimal(rt, path, buf) != 0){
        log_err("Parse failed for: %s", path);
        return -1;
    }
    log_info("Applied config: %s", path);
    return 0;
}

int config_load_all(runtime_cfg_t *rt, const char *main_path, const char *confdir){
    runtime_reset(rt);

    // Main
    if(load_one(rt, main_path) != 0) return -1;

    // Fragments (optionnels)
    char paths[256][512];
    int cnt = fs_list_yaml(confdir, paths, 256);
    if(cnt < 0){
        log_err("Failed to list confdir: %s", confdir);
        return -1;
    }
    for(int i=0;i<cnt;i++){
        if(load_one(rt, paths[i]) != 0) return -1;
    }

    log_info("Config merged OK (files=%d)", cnt+1);
    return 0;
}
