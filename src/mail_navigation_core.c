#include "mail_navigation_core.h"
#include <stddef.h>
#include <string.h>

int mail_navigation_filter(MailNavigation *s,MailListMode filter,const char *query,uint32_t cursor) {
    if(filter<MAIL_LIST_ALL || filter>MAIL_LIST_NEWS)return 0;
    if(filter==MAIL_LIST_SEARCH || filter==MAIL_LIST_NEWS) {
        if(!query || !*query || strlen(query)>80)return 0;
        for(const unsigned char *p=(const unsigned char *)query;*p;p++)
            if(*p<32 || *p==127)return 0;
        memmove(s->query,query,strlen(query)+1);
    }
    s->filter=filter;s->cursor=cursor;return 1;
}
MailListing mail_navigation_listing(const MailNavigation *s) {
    MailListing plan={.filter=s->filter,.cursor=s->cursor};
    if(s->filter==MAIL_LIST_SEARCH || s->filter==MAIL_LIST_NEWS)strcpy(plan.query,s->query);
    return plan;
}
void mail_navigation_clear_filter(MailNavigation *s) {
    volatile unsigned char *p=(volatile unsigned char *)s->query;
    for(size_t i=0;i<sizeof(s->query);i++)p[i]=0;
    s->filter=MAIL_LIST_ALL;s->cursor=0;
}
void mail_navigation_confirm(MailNavigation *s,uint32_t id) { s->delete_id=id; }
uint32_t mail_navigation_confirmed(MailNavigation *s,int yes) {
    uint32_t id=s->delete_id;s->delete_id=0;return yes?id:0;
}
void mail_navigation_observe(MailNavigation *s,MailSessionEvent event,uint32_t id) {
    if(id) { s->current=id;s->reading=event!=MAIL_EVENT_SEND; }
    if(event==MAIL_EVENT_LIST || event==MAIL_EVENT_FILES)s->reading=0;
}
void mail_navigation_page(MailNavigation *s,MailSessionEvent event,uint32_t next) {
    if(event==MAIL_EVENT_LIST || event==MAIL_EVENT_FILES)s->cursor=next;
}
ReaderPosition mail_navigation_start(int descending) {
    return (ReaderPosition){descending?UINT32_MAX:1,descending};
}
ReaderPosition mail_navigation_move(const MailNavigation *s,ReaderMove move,int descending,uint32_t distance) {
    return reader_move(move,s->current,descending,distance);
}
MailDraftEffect mail_draft_effect(MailSessionEvent event,uint32_t recipient,uint32_t id,uint32_t revision) {
    switch(event) {
    case MAIL_EVENT_BEGIN:case MAIL_EVENT_REPLY:case MAIL_EVENT_RESUME:
        return recipient?MAIL_DRAFT_OPEN:MAIL_DRAFT_KEEP;
    case MAIL_EVENT_SAVE:return revision?MAIL_DRAFT_SAVED:MAIL_DRAFT_KEEP;
    case MAIL_EVENT_SEND:return id?MAIL_DRAFT_SENT:MAIL_DRAFT_RETRY;
    case MAIL_EVENT_ATTACH:return recipient?MAIL_DRAFT_ATTACH:MAIL_DRAFT_KEEP;
    default:return MAIL_DRAFT_KEEP;
    }
}
