#include "batch_protocol.h"
#include "store_limits.h"
#include <stdio.h>
static int id_number(const char *p,uint32_t *number,const char **rest) {
    if(*p<'0' || *p>'9') return 0;
    uint32_t n=0;
    while(*p>='0' && *p<='9') { unsigned digit=(unsigned)(*p++-'0');if(n>(CONTENT_ID_MAX+1-digit)/10)return 0;n=n*10+digit; }
    if(!n) return 0;
    *number=n; *rest=p; return 1;
}

BatchIds batch_response_ids(const char *p) {
    BatchIds ids={0};
    while(*p==' ' && ids.count<BATCH_CAPACITY) {
        const char *tail;uint32_t id;if(!id_number(++p,&id,&tail))break;
        ids.ids[ids.count++]=id;p=tail;
    }
    return ids;
}
int batch_format_ids(const BatchIds *ids,char *out,size_t size) {
    if(!size)return 0;
    out[0]=0;if(ids->count>BATCH_CAPACITY)return 0;size_t used=0;
    for(unsigned i=0;i<ids->count;i++) {
        int n=snprintf(out+used,size-used,"%s%u",i?",":"",ids->ids[i]);
        if(n<0 || (size_t)n>=size-used) { out[0]=0;return 0; }used+=(size_t)n;
    }
    return 1;
}
int batch_download_receipt(const char *text,uint32_t *id) {
    unsigned value=0;*id=0;if(sscanf(text,"DOWNLOADED %u",&value)!=1)return 0;
    *id=value;return 1;
}
