#include "fs.h"
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

static int has_suffix(const char *s, const char *suf){
    size_t n=strlen(s), m=strlen(suf);
    return n>=m && strcmp(s+(n-m),suf)==0;
}

int fs_list_yaml(const char *dirpath, char out[][512], size_t max){
    DIR *d = opendir(dirpath);
    if (!d) return 0; // missing dir ≠ error

    size_t count = 0;
    for(struct dirent *e; (e=readdir(d))!=NULL; ){
        if(e->d_name[0]=='.') continue;
        if(!has_suffix(e->d_name, ".yaml")) continue;
        if(count<max){
            snprintf(out[count], 512, "%s/%s", dirpath, e->d_name);
            count++;
        }
    }
    closedir(d);

    // sort for deterministic merge order
    for(size_t i=0;i<count;i++)
      for(size_t j=i+1;j<count;j++)
        if(strcmp(out[i],out[j])>0){ char tmp[512]; strcpy(tmp,out[i]); strcpy(out[i],out[j]); strcpy(out[j],tmp); }

    return (int)count;
}

int fs_read_file(const char *path, char *buf, size_t buflen){
    FILE *f = fopen(path,"rb");
    if(!f) return -errno;
    size_t n = fread(buf,1,buflen-1,f);
    fclose(f);
    buf[n]=0;
    return (int)n;
}
