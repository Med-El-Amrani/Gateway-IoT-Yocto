#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <microhttpd.h>
#include "conn_http_server.h"

typedef struct { char* data; size_t len; size_t cap; } post_accum_t;

static int accum_append(post_accum_t* a, const char* data, size_t size){
    if(a->len + size + 1 > a->cap){
        size_t ncap = (a->cap? a->cap*2:1024);
        while(ncap < a->len+size+1) ncap*=2;
        char* nd = realloc(a->data, ncap);
        if(!nd) return 0;
        a->data = nd; a->cap = ncap;
    }
    memcpy(a->data + a->len, data, size);
    a->len += size;
    a->data[a->len] = '\0';
    return 1;
}

static enum MHD_Result send_response(struct MHD_Connection* c, unsigned code, const char* msg){
    struct MHD_Response* r = MHD_create_response_from_buffer(
        msg? strlen(msg):0, (void*)(msg?msg:""), MHD_RESPMEM_PERSISTENT);
    enum MHD_Result ret = MHD_queue_response(c, code, r);
    MHD_destroy_response(r);
    return ret;
}

static int parse_bind_port(const char* bind){
    /* "0.0.0.0:8081" -> 8081 ; "8081" -> 8081 ; NULL -> 8080 */
    if(!bind) return 8080;
    const char* c = strrchr(bind, ':');
    if(c) return atoi(c+1);
    int p = atoi(bind);
    return p > 0 ? p : 8080;
}

static int allow_route(const http_server_connector_t* cfg, const char* path){
    if(!cfg) return 0;
    if(cfg->params.routes_count==0) return 1; /* pas de liste -> tout accepté */
    for(size_t i=0;i<cfg->params.routes_count;i++){
        const char* r = cfg->params.routes[i].path;
        if(r && strcmp(r, path)==0) return 1;
    }
    return 0;
}

/* Handler MHD : ne fait que collecter les POST et appeler le callback RX */
static enum MHD_Result on_access(void* cls,
                   struct MHD_Connection* c,
                   const char* url,
                   const char* method,
                   const char* version,
                   const char* upload_data,
                   size_t* upload_data_size,
                   void** con_cls)
{
    (void)version;
    http_server_runtime_t* rt = (http_server_runtime_t*)cls;

    if(*con_cls == NULL){
        post_accum_t* a = calloc(1, sizeof(*a));
        *con_cls = a;
        return MHD_YES; // premier appel → allouer l'accumulateur
    }
    post_accum_t* a = (post_accum_t*)(*con_cls);

    if(strcmp(method,"POST")==0){
        if(*upload_data_size){
            if(!accum_append(a, upload_data, *upload_data_size))
                return send_response(c, MHD_HTTP_INTERNAL_SERVER_ERROR, "oom");
            *upload_data_size = 0;
            return MHD_YES; // continuer la réception
        } else {
            if(!allow_route(rt->cfg, url)){
                free(a->data); free(a); *con_cls=NULL;
                return send_response(c, MHD_HTTP_NOT_FOUND, "route not allowed");
            }
            /* Appeler le callback RX si présent */
            int rc = -1;
            if(rt->on_rx){
                rc = rt->on_rx(url, a->data ? a->data : "", a->len, rt->on_rx_user);
            }
            free(a->data); free(a); *con_cls=NULL;
            if(rc==0) return send_response(c, MHD_HTTP_OK, "ok");
            return send_response(c, MHD_HTTP_INTERNAL_SERVER_ERROR, "handler failed");
        }
    }

    return send_response(c, MHD_HTTP_METHOD_NOT_ALLOWED, "POST only");
}


int conn_http_server_start_from_config(const http_server_connector_t* cfg,
                                       http_server_runtime_t* rt)
{
    if(!cfg || !rt) return -1;
    memset(rt, 0, sizeof(*rt));
    rt->cfg = cfg;

    int port = parse_bind_port(cfg->params.bind ? cfg->params.bind : "0.0.0.0:8080");

    rt->d = MHD_start_daemon(MHD_USE_AUTO | MHD_USE_INTERNAL_POLLING_THREAD,
                             (uint16_t)port,
                             NULL, NULL,
                             &on_access, rt,
                             MHD_OPTION_END);
    if(!rt->d){
        perror("MHD_start_daemon");
        return -1;
    }
    rt->port = port;
    printf("[http] listening on :%d\n", port);
    return 0;
}


int conn_http_server_set_rx_cb(http_server_runtime_t* rt, http_rx_cb cb, void* user){
    if(!rt) return -1;
    rt->on_rx = cb;
    rt->on_rx_user = user;
    return 0;
}

void conn_http_server_stop(http_server_runtime_t* rt){
    if(!rt || !rt->d) return;
    MHD_stop_daemon(rt->d);
    rt->d = NULL;
}



int http_normalize(const char* url, const void* body, size_t len, gw_msg_t* out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->protocole = KIND_HTTP_SERVER;          // or KIND_HTTP if you prefer
    out->params.http_server.bind = (char*)(url ? url : "");
    out->pl.data = (const uint8_t*)(body ? body : (const void*)"");
    out->pl.len  = len;
    out->pl.is_text = 1;                        // hint
    return 0;
}

int on_http_rx(const char* url, const void* body, size_t len, void* user) {
    gw_bridge_runtime_t* b = (gw_bridge_runtime_t*)user;
    if (!b) return -1;

    gw_msg_t in;
    if (http_normalize(url, body, len, &in) != 0) {
        fprintf(stderr, "[bridge:%s] http_normalize failed\n", b->id);
        return -1;
    }

    gw_msg_t out = {0};
    const gw_transform_fn tf = b->transform;
    void* tf_user = b->transform_user;

    if (tf) {
        if (tf(&in, &out, tf_user) != 0) {
            fprintf(stderr, "[bridge:%s] transform failed\n", b->id);
            return -1;
        }
    } else {
        /* No transform: pass-through (rare). Caller must have a sender that
           understands KIND_HTTP messages. */
        out = in;
    }

    if (!b->send_fn || !b->send_ctx) {
        fprintf(stderr, "[bridge:%s] send_fn/send_ctx not set\n", b->id);
        return -1;
    }
    return b->send_fn(&out, b->send_ctx);
}