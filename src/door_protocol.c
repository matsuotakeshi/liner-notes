#include "door_protocol.h"
#include <stdio.h>
#include <string.h>

int door_protocol_list(const DoorListing *listing,char *out,size_t size) {
    if(!out || !size)return 0;
    out[0]=0;
    if(listing->count>DOOR_CATALOG_LIMIT)return 0;
    size_t used=0;int n=snprintf(out,size,"GAMES\r\n");
    if(n<0 || (size_t)n>=size)goto full;
    used=(size_t)n;
    for(unsigned i=0;i<listing->count;i++) {
        const DoorSummary *game=&listing->games[i];
        n=snprintf(out+used,size-used,"GAME %u %u %s\r\n",game->id,game->seconds,game->title);
        if(n<0 || (size_t)n>=size-used)goto full;
        used+=(size_t)n;
    }
    n=snprintf(out+used,size-used,"END GAMES\r\n");
    if(n<0 || (size_t)n>=size-used)goto full;
    return 1;
full:
    memset(out,0,size);return 0;
}
