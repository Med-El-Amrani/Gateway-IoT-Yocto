#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>
#include <errno.h>
#include <assert.h>

#include "connector_registry.h"
#include "params_parsers.h"   // add this near other includes



#include <limits.h>

/* --- path utils (no dirname/basename side effects) --- */
static void path_dirname(const char* path, char* out, size_t outsz){
    size_t n = strlen(path);
    if(n==0){ snprintf(out, outsz, "."); return; }
    const char* slash = NULL;
    for(size_t i=0;i<n;i++) if(path[i]=='/') slash = path+i;
    if(!slash){ snprintf(out, outsz, "."); return; }
    size_t len = (size_t)(slash - path);
    if(len==0) len = 1; /* "/" */
    if(len >= outsz) len = outsz-1;
    memcpy(out, path, len);
    out[len] = '\0';
}
static int path_is_abs(const char* p){
    return p && p[0]=='/'; /* (Yocto env: we keep it simple) */
}
static void path_join2(const char* a, const char* b, char* out, size_t outsz){
    if(!a || !*a){ snprintf(out,outsz,"%s", b?b:""); return; }
    if(!b || !*b){ snprintf(out,outsz,"%s", a); return; }
    if(a[strlen(a)-1]=='/')
        snprintf(out,outsz,"%s%s", a,b);
    else
        snprintf(out,outsz,"%s/%s", a,b);
}


/* ---------- utils ---------- */
static char* xstrdup(const char* s){ if(!s) return NULL; size_t n=strlen(s)+1; char* p=malloc(n); if(p) memcpy(p,s,n); return p; }
static void* xcalloc(size_t n, size_t sz){ void* p = calloc(n, sz); if(!p){ perror("calloc"); exit(1);} return p; }
static void  append_str(char*** arr, size_t* n, const char* s){
    *arr = realloc(*arr, (*n+1)*sizeof(char*)); if(!*arr){perror("realloc"); exit(1);}
    (*arr)[*n] = xstrdup(s); (*n)++;
}




/* ---- minimal YAML helpers ---- */
typedef struct { yaml_document_t doc; } ydoc_t;

static int yload(const char* path, ydoc_t* out){
    FILE* f = fopen(path, "rb");
    if(!f){ fprintf(stderr,"open %s: %s\n", path, strerror(errno)); return -1; }
    yaml_parser_t p; yaml_parser_initialize(&p);
    yaml_parser_set_input_file(&p, f);
    yaml_document_initialize(&out->doc, NULL, NULL, NULL, 0, 0);
    if(!yaml_parser_load(&p, &out->doc)){
        fprintf(stderr, "YAML parse error in %s\n", path);
        yaml_parser_delete(&p); fclose(f); return -1;
    }
    yaml_parser_delete(&p); fclose(f);
    return yaml_document_get_root_node(&out->doc) ? 0 : -1;
}
static void yfree(ydoc_t* d){ yaml_document_delete(&d->doc); }

static yaml_node_t* ymap_get(yaml_document_t* doc, yaml_node_t* map, const char* key){
    if(!map || map->type != YAML_MAPPING_NODE) return NULL;
    for(yaml_node_pair_t* p = map->data.mapping.pairs.start; p < map->data.mapping.pairs.top; ++p){
        yaml_node_t* k = yaml_document_get_node(doc, p->key);
        yaml_node_t* v = yaml_document_get_node(doc, p->value);
        if(k && k->type == YAML_SCALAR_NODE && k->data.scalar.value && strcmp((char*)k->data.scalar.value, key)==0)
            return v;
    }
    return NULL;
}
static const char* yscalar_str(yaml_node_t* n){ return (n && n->type==YAML_SCALAR_NODE) ? (const char*)n->data.scalar.value : NULL; }
static long yscalar_int(yaml_node_t* n, int* ok){ if(!n||n->type!=YAML_SCALAR_NODE){if(ok)*ok=0;return 0;} char* e=NULL; long v=strtol((char*)n->data.scalar.value,&e,10); if(ok)*ok=(e&&*e=='\0'); return v; }
static double yscalar_num(yaml_node_t* n, int* ok){ if(!n||n->type!=YAML_SCALAR_NODE){if(ok)*ok=0;return 0;} char* e=NULL; double v=strtod((char*)n->data.scalar.value,&e); if(ok)*ok=(e&&*e=='\0'); return v; }

/* ---- cleanup helpers ---- */
static void free_bridge_mapping(bridge_mapping_t* m){
    if(!m) return;
    free(m->topic);
    for(size_t i=0;i<m->fields_count;i++) free(m->fields[i]);
    free(m->fields);
}

/* Public cleanup */
void config_free(config_t* cfg){
    if(!cfg) return;

    free(cfg->gateway.name);
    free(cfg->gateway.timezone);
    free(cfg->gateway.loglevel);
    free(cfg->gateway.logfile);

    for(size_t i=0;i<cfg->includes.count;i++) free(cfg->includes.paths[i]);
    free(cfg->includes.paths);

    // Connectors
    for(size_t i=0;i<cfg->connectors.count;i++){
        connector_any_t* c = &cfg->connectors.items[i];
        free(c->name);
        for(size_t t=0;t<c->tags_count;t++) free(c->tags[t]);
        free(c->tags);

        // If connector was loaded via opaque fallback, free JSON blob
        if (c->u.opaque.json_params) {
            free(c->u.opaque.json_params);
        }

        /* TODO: deep-free typed fields if you malloc()ed inside,
           e.g., free(c->u.mqtt.params.client_id) etc.
           (Omitted here to keep it short.) */
    }
    free(cfg->connectors.items);

    // Bridges
    for(size_t i=0;i<cfg->bridges.count;i++){
        bridge_t* b = &cfg->bridges.items[i];
        free(b->name);
        free(b->from);
        free(b->to);
        free_bridge_mapping(&b->mapping);
        for(size_t j=0;j<b->transform_count;j++) free(b->transform[j]);
        free(b->transform);
    }
    free(cfg->bridges.items);
}


/* ---- tiny YAML->JSON serializer for a node subtree (sufficient for params) ---- */
static void json_escape(const char* s, FILE* out){
    for(const unsigned char* p=(const unsigned char*)s; *p; ++p){
        if(*p=='"'||*p=='\\') fputc('\\', out), fputc(*p, out);
        else if(*p=='\n') fputs("\\n", out);
        else fputc(*p, out);
    }
}
static void node_to_json(yaml_document_t* d, yaml_node_t* n, FILE* out){
    switch(n->type){
    case YAML_SCALAR_NODE:{
        const char* v = (const char*)n->data.scalar.value;
        /* Decide num/bool/null vs string: since your Python validator ran earlier,
           we could keep as string reliably, but we try a tiny detection: */
        if(!v) { fputs("null", out); break; }
        /* bool */
        if(strcmp(v,"true")==0 || strcmp(v,"false")==0){ fputs(v, out); break; }
        /* number */
        char* e=NULL; strtod(v,&e);
        if(e && *e=='\0'){ fputs(v, out); break; }
        /* else string */
        fputc('"', out); json_escape(v, out); fputc('"', out); break;
    }
    case YAML_SEQUENCE_NODE:{
        fputc('[', out);
        bool first=true;
        for(yaml_node_item_t* it = n->data.sequence.items.start; it < n->data.sequence.items.top; ++it){
            if(!first) {fputc(',', out);} first=false;
            node_to_json(d, yaml_document_get_node(d, *it), out);
        }
        fputc(']', out); break;
    }
    case YAML_MAPPING_NODE:{
        fputc('{', out);
        bool first=true;
        for(yaml_node_pair_t* p = n->data.mapping.pairs.start; p < n->data.mapping.pairs.top; ++p){
            yaml_node_t* k = yaml_document_get_node(d, p->key);
            yaml_node_t* v = yaml_document_get_node(d, p->value);
            if(k && k->type==YAML_SCALAR_NODE){
                if(!first) {fputc(',', out);} first=false;
                fputc('"', out); json_escape((char*)k->data.scalar.value, out); fputc('"', out); fputc(':', out);
                node_to_json(d, v, out);
            }
        }
        fputc('}', out); break;
    }
    default: fputs("null", out);
    }
}
static char* serialize_node_json(yaml_document_t* d, yaml_node_t* n){
    FILE* mem = tmpfile();
    if(!mem){ perror("tmpfile"); return NULL; }
    node_to_json(d, n, mem);
    fflush(mem);
    long sz = ftell(mem);
    if(sz < 0){ fclose(mem); return NULL; }
    rewind(mem);
    char* buf = malloc((size_t)sz+1);
    if(!buf){ fclose(mem); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, mem);
    fclose(mem);
    if(rd != (size_t)sz){ free(buf); return NULL; }
    buf[sz] = '\0';
    return buf;
}


/* ---- parse Gateway, Includes, Bridges (schema-generic) ---- */
static map_format_t parse_format(const char* s){
    if(!s) return MAP_FMT_JSON;
    if(strcmp(s,"json")==0) return MAP_FMT_JSON;
    if(strcmp(s,"kv")==0)   return MAP_FMT_KV;
    return MAP_FMT_RAW;
}
static buffer_policy_t parse_policy(const char* s){
    return (s && strcmp(s,"drop_new")==0) ? BUF_DROP_NEW : BUF_DROP_OLDEST;
}

static int parse_gateway(yaml_document_t* doc, yaml_node_t* gw_map, gateway_cfg_t* gw){
    if(!gw_map || gw_map->type!=YAML_MAPPING_NODE) return -1;
    const char* s;
    s = yscalar_str( ymap_get(doc, gw_map, "name") );     gw->name     = s? xstrdup(s): NULL;
    s = yscalar_str( ymap_get(doc, gw_map, "timezone") ); gw->timezone = s? xstrdup(s): NULL;
    s = yscalar_str( ymap_get(doc, gw_map, "loglevel") ); gw->loglevel = s? xstrdup(s): NULL;
    s = yscalar_str( ymap_get(doc, gw_map, "logfile") );  gw->logfile  = s? xstrdup(s): NULL;
    int ok=0; long mp = yscalar_int( ymap_get(doc, gw_map, "metrics_port"), &ok );
    if(ok){ gw->metrics_port = (int)mp; gw->metrics_port_set = true; }
    return 0;
}

static int parse_includes(yaml_document_t* doc, yaml_node_t* seq, include_list_t* incs){
    if(!seq || seq->type!=YAML_SEQUENCE_NODE) return 0;
    for(yaml_node_item_t* it = seq->data.sequence.items.start; it < seq->data.sequence.items.top; ++it){
        yaml_node_t* item = yaml_document_get_node(doc, *it);
        const char* s = yscalar_str(item);
        if(s) append_str(&incs->paths, &incs->count, s);
    }
    return 0;
}

static int parse_bridge_one(yaml_document_t* doc, const char* name, yaml_node_t* bmap, bridge_t* out){
    memset(out, 0, sizeof(*out));
    out->name = xstrdup(name);
    const char* s;
    s = yscalar_str( ymap_get(doc, bmap, "from") ); if(s) out->from = xstrdup(s);
    s = yscalar_str( ymap_get(doc, bmap, "to") );   if(s) out->to   = xstrdup(s);

    yaml_node_t* m = ymap_get(doc, bmap, "mapping");
    if(m && m->type==YAML_MAPPING_NODE){
        s = yscalar_str( ymap_get(doc, m, "topic") ); if(s) out->mapping.topic = xstrdup(s);
        out->mapping.format = parse_format( yscalar_str( ymap_get(doc, m, "format") ) );
        yaml_node_t* fields = ymap_get(doc, m, "fields");
        if(fields && fields->type==YAML_SEQUENCE_NODE){
            size_t n = (fields->data.sequence.items.top - fields->data.sequence.items.start);
            out->mapping.fields = n? xcalloc(n, sizeof(char*)) : NULL;
            out->mapping.fields_count = 0;
            for(yaml_node_item_t* it = fields->data.sequence.items.start; it < fields->data.sequence.items.top; ++it){
                yaml_node_t* fn = yaml_document_get_node(doc, *it);
                const char* fs = yscalar_str(fn);
                if(fs) out->mapping.fields[out->mapping.fields_count++] = xstrdup(fs);
            }
        }
        const char* ts = yscalar_str( ymap_get(doc, m, "timestamp") );
        if(ts){ out->mapping.timestamp = (!strcmp(ts,"true")||!strcmp(ts,"1")); out->mapping.timestamp_set=true; }
    }

    yaml_node_t* t = ymap_get(doc, bmap, "transform");
    if(t && t->type==YAML_SEQUENCE_NODE){
        size_t n = (t->data.sequence.items.top - t->data.sequence.items.start);
        out->transform = n? xcalloc(n, sizeof(char*)) : NULL;
        out->transform_count = 0;
        for(yaml_node_item_t* it = t->data.sequence.items.start; it < t->data.sequence.items.top; ++it){
            yaml_node_t* tn = yaml_document_get_node(doc, *it);
            const char* ts = yscalar_str(tn);
            if(ts) out->transform[out->transform_count++] = xstrdup(ts);
        }
    }

    yaml_node_t* rl = ymap_get(doc, bmap, "rate_limit");
    if(rl && rl->type==YAML_MAPPING_NODE){
        int ok=0; double mps = yscalar_num( ymap_get(doc, rl, "max_msgs_per_sec"), &ok );
        if(ok){ out->rate_limit.max_msgs_per_sec = mps; out->rate_limit.has_max_msgs_per_sec = true; }
        ok=0; long burst = yscalar_int( ymap_get(doc, rl, "burst"), &ok );
        if(ok){ out->rate_limit.burst = (int)burst; out->rate_limit.has_burst = true; }
    }

    yaml_node_t* buf = ymap_get(doc, bmap, "buffer");
    if(buf && buf->type==YAML_MAPPING_NODE){
        int ok=0; long sz = yscalar_int( ymap_get(doc, buf, "size"), &ok );
        if(ok){ out->buffer.size = (int)sz; out->buffer.has_size = true; }
        out->buffer.policy = parse_policy( yscalar_str( ymap_get(doc, buf, "policy") ) );
        out->buffer.has_policy = (ymap_get(doc, buf, "policy") != NULL);
    }
    return 0;
}

static int parse_bridges(yaml_document_t* doc, yaml_node_t* br_map, bridges_table_t* out){
    if(!br_map || br_map->type!=YAML_MAPPING_NODE) return 0;
    size_t cap = (br_map->data.mapping.pairs.top - br_map->data.mapping.pairs.start);
    out->items = cap? xcalloc(cap, sizeof(bridge_t)) : NULL;
    out->count = 0;
    for(yaml_node_pair_t* p = br_map->data.mapping.pairs.start; p < br_map->data.mapping.pairs.top; ++p){
        yaml_node_t* k = yaml_document_get_node(doc, p->key);
        yaml_node_t* v = yaml_document_get_node(doc, p->value);
        if(!k || k->type!=YAML_SCALAR_NODE || !v || v->type!=YAML_MAPPING_NODE) continue;
        parse_bridge_one(doc, (const char*)k->data.scalar.value, v, &out->items[out->count++]);
    }
    return 0;
}

/* ---- generic connector parsing using registry; includes tags ---- */
static int parse_tags(yaml_document_t* doc, yaml_node_t* tags_node, connector_any_t* out){
    if(!tags_node || tags_node->type!=YAML_SEQUENCE_NODE) return 0;
    size_t n = (tags_node->data.sequence.items.top - tags_node->data.sequence.items.start);
    out->tags = n? xcalloc(n, sizeof(char*)) : NULL;
    out->tags_count = 0;
    for(yaml_node_item_t* it = tags_node->data.sequence.items.start; it < tags_node->data.sequence.items.top; ++it){
        yaml_node_t* tn = yaml_document_get_node(doc, *it);
        const char* s = yscalar_str(tn);
        if(s) out->tags[out->tags_count++] = xstrdup(s);
    }
    return 0;
}

/* Opaque fallback: keep params as normalized JSON string */
static int fill_opaque_params(yaml_document_t* doc, yaml_node_t* conn_map, connector_any_t* out){
    yaml_node_t* params = ymap_get(doc, conn_map, "params");
    if(!params) return 0;
    out->u.opaque.json_params = serialize_node_json(doc, params);
    return 0;
}

/* One connector item (keyed) */
static int parse_connector_item(yaml_document_t* doc, const char* key, yaml_node_t* conn_map, connectors_table_t* table){
    connector_any_t tmp; memset(&tmp, 0, sizeof(tmp));
    tmp.name = xstrdup(key);

    const char* type_s = yscalar_str( ymap_get(doc, conn_map, "type") );
    const connector_registry_entry_t* e = reg_lookup(type_s);
    tmp.kind = e ? e->kind : KIND_UNKNOWN;

    /* tags (generic) */
    parse_tags(doc, ymap_get(doc, conn_map, "tags"), &tmp);

    /* Dispatch to registered parser, or opaque fallback */
    int rc = 0;
    if(e && e->parse){
        rc = e->parse(doc, conn_map, &tmp);
    } else {
        rc = fill_opaque_params(doc, conn_map, &tmp);
        if(tmp.kind == KIND_UNKNOWN){
            fprintf(stderr, "WARN: unknown connector type '%s' for '%s' -> stored as opaque.\n",
                    type_s?type_s:"(null)", key);
        }
    }
    if(rc==0){
        table->items = realloc(table->items, (table->count+1)*sizeof(connector_any_t));
        if(!table->items){ perror("realloc"); exit(1); }
        table->items[table->count++] = tmp;
    } else {
        /* free tmp.name/tmp.tags on error, omitted for brevity */
    }
    return rc;
}

static int parse_connectors_map(yaml_document_t* doc, yaml_node_t* conns_map, connectors_table_t* table){
    if(!conns_map || conns_map->type!=YAML_MAPPING_NODE) return 0;
    for(yaml_node_pair_t* p = conns_map->data.mapping.pairs.start; p < conns_map->data.mapping.pairs.top; ++p){
        yaml_node_t* k = yaml_document_get_node(doc, p->key);
        yaml_node_t* v = yaml_document_get_node(doc, p->value);
        const char* id = (k && k->type==YAML_SCALAR_NODE)? (const char*)k->data.scalar.value : NULL;
        if(!id || !v || v->type!=YAML_MAPPING_NODE) continue;
        parse_connector_item(doc, id, v, table);
    }
    return 0;
}

/* ---- public API ---- */
int config_load_file(const char* path, config_t* cfg){
    memset(cfg, 0, sizeof(*cfg));

    ydoc_t d;
    if(yload(path, &d)!=0) return -1;
    yaml_node_t* root = yaml_document_get_root_node(&d.doc);
    if(!root || root->type!=YAML_MAPPING_NODE){ yfree(&d); return -1; }

    /* version, gateway, includes, connectors, bridges */
    int ok=0;
    yaml_node_t* v = ymap_get(&d.doc, root, "version");
    if(v){ cfg->version = yscalar_num(v, &ok); if(ok) cfg->version_set = true; }

    parse_gateway(&d.doc, ymap_get(&d.doc, root, "gateway"), &cfg->gateway);
    parse_includes(&d.doc, ymap_get(&d.doc, root, "includes"), &cfg->includes);
    parse_connectors_map(&d.doc, ymap_get(&d.doc, root, "connectors"), &cfg->connectors);
    parse_bridges(&d.doc, ymap_get(&d.doc, root, "bridges"), &cfg->bridges);
    yfree(&d);

/* merge includes (resolve relative to the config file's directory) */
{
    char base_dir[PATH_MAX]; path_dirname(path, base_dir, sizeof(base_dir));
    for(size_t i=0;i<cfg->includes.count;i++){
        char resolved[PATH_MAX];
        const char* incp = cfg->includes.paths[i];
        if(path_is_abs(incp)) snprintf(resolved, sizeof(resolved), "%s", incp);
        else                  path_join2(base_dir, incp, resolved, sizeof(resolved));

        ydoc_t inc;
        if(yload(resolved, &inc)==0){
            yaml_node_t* r = yaml_document_get_root_node(&inc.doc);
            parse_connectors_map(&inc.doc, ymap_get(&inc.doc, r, "connectors"), &cfg->connectors);
            yfree(&inc);
        } else {
            fprintf(stderr, "WARN: cannot load include %s (resolved: %s)\n", incp, resolved);
        }
    }
}

    return 0;
}

/* Lookup helper */
const connector_any_t* config_find_connector(const config_t* cfg, const char* name){
    for(size_t i=0;i<cfg->connectors.count;i++)
        if(cfg->connectors.items[i].name && strcmp(cfg->connectors.items[i].name, name)==0)
            return &cfg->connectors.items[i];
    return NULL;
}
