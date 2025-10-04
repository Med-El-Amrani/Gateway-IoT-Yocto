// src/main.c
#include "app.h"
#include <stdio.h>
#include <string.h>

#ifndef IOTGWD_VERSION
#define IOTGWD_VERSION "dev"
#endif

int main(int argc, char **argv){
    const char *cfg = "/etc/iotgw.yaml";
    const char *dir = "/etc/iotgwd";

    for (int i=1; i<argc; ++i){
        if ((strcmp(argv[i],"-c")==0 || strcmp(argv[i],"--config")==0) && i+1<argc) cfg = argv[++i];
        else if (strcmp(argv[i],"--confdir")==0 && i+1<argc) dir = argv[++i];
        else if (strcmp(argv[i],"--version")==0){ printf("iotgwd %s\n", IOTGWD_VERSION); return 0; }
        else if (strcmp(argv[i],"-h")==0 || strcmp(argv[i],"--help")==0){
            printf("Usage: %s [-c FILE|--config FILE] [--confdir DIR]\n", argv[0]);
            return 0;
        }
    }

    app_ctx_t app = {
        .cfg_file = cfg,
        .cfg_dir  = dir,
        .stop     = 0,
        .reload   = 0,
    };
    return app_run(&app);
}
