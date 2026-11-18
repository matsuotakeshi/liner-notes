#include "connection_protocol.h"
#include <stdio.h>
#include <string.h>

int connection_protocol_list(const ConnectionSummary *rows,size_t count,char *out,size_t size) {
    static const char *modes[]={"BBS","EDITOR","CHAT","AUTH","BINARY","ZMODEM","DOOR"};
    if(!out || !size)return 0;
    out[0]=0;int n=snprintf(out,size,"CONNECTIONS\r\n");size_t used=0;
    if(n<0 || (size_t)n>=size)goto full;
    used=(size_t)n;
    for(size_t i=0;i<count;i++) {
        const ConnectionSummary *r=&rows[i];
        if((unsigned)r->mode>=sizeof(modes)/sizeof(*modes))goto full;
        n=snprintf(out+used,size-used,"CONNECTION %lu %s %s %s %s %llu %llu %llu %llu %zu %u %u\r\n",
            r->id,r->login,modes[r->mode],r->telnet?"TELNET":"RAW",*r->peer?r->peer:"unknown",
            (unsigned long long)r->elapsed,(unsigned long long)r->idle,(unsigned long long)r->rx,
            (unsigned long long)r->tx,r->queued,r->columns,r->rows);
        if(n<0 || (size_t)n>=size-used)goto full;
        used+=(size_t)n;
    }
    n=snprintf(out+used,size-used,"END CONNECTIONS\r\n");
    if(n<0 || (size_t)n>=size-used)goto full;
    return 1;
full:
    memset(out,0,size);return 0;
}
