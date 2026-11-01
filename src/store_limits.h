#ifndef STORE_LIMITS_H
#define STORE_LIMITS_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* Bit 31 tags compact article records. Reserve one ID for end cursors. */
#define CONTENT_ID_MAX 0x7ffffffeu
extern uint32_t limit_articles,limit_mail,limit_user_posts;
/* Grow before writing journals, with checked arithmetic and zeroed new slots. */
static inline int store_reserve(void **data,size_t *capacity,size_t need,size_t width) {
    if(need<=*capacity)return 1;
    size_t next=*capacity?*capacity:64;
    while(next<need) { if(next>SIZE_MAX/2) { next=need;break; } next*=2; }
    if(next>SIZE_MAX/width)return 0;
    void *copy=realloc(*data,next*width);if(!copy)return 0;
    memset((char *)copy+*capacity*width,0,(next-*capacity)*width);
    *data=copy;*capacity=next;return 1;
}
#endif
