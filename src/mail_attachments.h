#ifndef MAIL_ATTACHMENTS_H
#define MAIL_ATTACHMENTS_H
#include <stdint.h>
#define MAIL_ATTACH_MAX 16
typedef struct { uint32_t count,ids[MAIL_ATTACH_MAX]; } MailAttachments;
static inline int mail_has(const MailAttachments *a,uint32_t id) {
    if(!id)return 0;
    for(uint32_t i=0;i<a->count;i++)if(a->ids[i]==id)return 1;
    return 0;
}
#endif
