#include "config_loader.h"
#include "fs.h"
#include "log.h"
#include <string.h>

// ---- Minimal “parser” stub --------------------------------------------------
// For now, we only simulate loads so the daemon runs.
// Later: replace with real YAML parsing + JSON-schema validation if needed.

static int parse_and_apply_yaml(runtime_cfg_t *rt, const char *path, const char *content){
    (void)content; // unused in stub
    log_info("Applied config from: %s", path);
    rt->files_loaded++;
    return 0;
}

// -----------------------------------------------------------------------------

int config_load_all(runtime_cfg_t *rt, const char *main_path, const char *confdir){
    runtime_reset(rt);

    // 1) main config
    char buf[64*1024]; // enough for typical configs
    int n = fs_read_file(main_path, buf, sizeof(buf));
    if(n < 0){
        log_err("Cannot read main config: %s (err=%d)", main_path, n);
        return 1;
    }
    if(parse_and_apply_yaml(rt, main_path, buf) != 0){
        log_err("Parse failed for: %s", main_path);
        return 1;
    }

    // 2) fragments
    char paths[256][512];
    int cnt = fs_list_yaml(confdir, paths, 256);
    if(cnt < 0){
        log_err("Failed listing confdir: %s", confdir);
        return 1;
    }
    for(int i=0;i<cnt;i++){
        int m = fs_read_file(paths[i], buf, sizeof(buf));
        if(m < 0){ log_warn("Skip unreadable fragment: %s (err=%d)", paths[i], m); continue; }
        if(parse_and_apply_yaml(rt, paths[i], buf) != 0){
            log_err("Fragment parse failed: %s", paths[i]);
            return 1;
        }
        rt->fragments_loaded++;
    }

    log_info("Config loaded: files=%zu fragments=%zu", rt->files_loaded, rt->fragments_loaded);
    return 0;
}
