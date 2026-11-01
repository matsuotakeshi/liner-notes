#ifndef LN_RUNTIME_LIMITS_H
#define LN_RUNTIME_LIMITS_H
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "app_limits.h"
#include "content_limits.h"
/* Immutable after startup. Lower quotas never invalidate previously saved data. */
static inline uint64_t ln_limit(const char *name,uint64_t maximum) {
    char key[80];snprintf(key,sizeof(key),"LINER_LIMIT_%s",name);const char *v=getenv(key);
    if(!v || !*v)return maximum;
    char *end;unsigned long long n=strtoull(v,&end,10);return !*end && n && n<=maximum?(uint64_t)n:maximum;
}
static inline int ln_limits_load(const char *path) {
    static const struct { const char *key,*env;uint64_t max; } fields[]={
        {"members","MEMBERS",MEMBER_MAX},{"boards","BOARDS",1024},
        {"input_bytes","INPUT",INPUT_LINE_MAX},{"body_bytes","BODY",CONTENT_BODY},
        {"connections","CONNECTIONS",CONNECTION_MAX},{"transfers","TRANSFERS",ZMODEM_MAX},
        {"file_bytes","FILE",UINT64_C(1073741824)},{"batch_files","BATCH_FILES",16},
        {"batch_bytes","BATCH_BYTES",UINT64_C(17179869184)}
    };
    FILE *f=fopen(path,"r");if(!f)return 0;char line[256];unsigned seen=0;int ok=1;
    while(ok && fgets(line,sizeof(line),f)) {
        size_t n=strcspn(line,"\r\n");if(!line[n] && !feof(f)) { ok=0;break; }line[n]=0;
        if(!*line || *line=='#')continue;
        char *sep=strchr(line,'=');if(!sep) { ok=0;break; }*sep++=0;
        unsigned i;for(i=0;i<sizeof(fields)/sizeof(*fields);i++)if(!strcmp(line,fields[i].key))break;
        if(i==sizeof(fields)/sizeof(*fields) || (seen&(1u<<i)) || !*sep || strspn(sep,"0123456789")!=strlen(sep)) { ok=0;break; }
        char *end;unsigned long long value=strtoull(sep,&end,10);
        if(*end || !value || value>fields[i].max) { ok=0;break; }
        char key[80];snprintf(key,sizeof(key),"LINER_LIMIT_%s",fields[i].env);
#ifdef _WIN32
        if(_putenv_s(key,sep))ok=0;
#else
        if(setenv(key,sep,1))ok=0;
#endif
        seen|=1u<<i;
    }
    if(ferror(f))ok=0;if(fclose(f))ok=0;return ok;
}
#endif
