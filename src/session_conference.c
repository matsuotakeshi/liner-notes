/* RTBBS presentation and wire syntax for the shared line conference. */
#include "session_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int number(const char **args,uint32_t *value,int zero) {
    const char *p=*args;
    if(zero && p[0]=='0' && (!p[1] || p[1]==' ')) {
        *value=0;*args=p+1;return 1;
    }
    return session_impl_number_arg(p,value,args);
}

static int parse(const char *args,ConferenceRequest *q) {
    memset(q,0,sizeof(*q));
    if(!*args)return 1;
    struct { const char *name; ConferenceAction action; } verbs[]={
        {"ROOMS",CONFERENCE_LIST},{"HISTORY",CONFERENCE_HISTORY},{"NEW",CONFERENCE_UNREAD},
        {"POST",CONFERENCE_POST},{"POSITION",CONFERENCE_POSITION},{"SEEN",CONFERENCE_SEEN},{"CREATE",CONFERENCE_CREATE}
    };
    size_t word=strcspn(args," ");
    unsigned selected=0;
    while(selected<sizeof(verbs)/sizeof(*verbs) &&
          (strlen(verbs[selected].name)!=word || strncmp(args,verbs[selected].name,word)))selected++;
    if(selected==sizeof(verbs)/sizeof(*verbs))return 0;
    q->action=verbs[selected].action;
    args+=word;
    while(*args==' ')args++;
    if(q->action==CONFERENCE_LIST)return !*args || (number(&args,&q->cursor,1) && !*args);
    if(!*args)return q->action==CONFERENCE_UNREAD;
    if(!number(&args,&q->room,q->action==CONFERENCE_UNREAD || q->action==CONFERENCE_SEEN))return 0;
    while(*args==' ')args++;
    if(q->action==CONFERENCE_POST || q->action==CONFERENCE_CREATE) {
        size_t size=strlen(args);
        if(!size || size>CONFERENCE_TEXT)return 0;
        memcpy(q->text,args,size+1);return 1;
    }
    if(q->action==CONFERENCE_POSITION)return !*args;
    if(!*args)return q->action!=CONFERENCE_SEEN;
    return number(&args,&q->cursor,q->action!=CONFERENCE_SEEN) && !*args;
}

int session_impl_conference_command(Session *s,char *line) {
    if(strncmp(line,"LINECONF",8) || (line[8] && line[8]!=' '))return 0;
    ConferenceRequest request;
    const char *args=line+8;
    while(*args==' ')args++;
    if(!parse(args,&request)) {
        session_impl_emit(s,"ERR LINECONF syntax; HELP LINECONF\r\n");return 1;
    }
    OperationRequest *q=calloc(1,sizeof(*q));
    if(!q) { session_impl_emit(s,"ERR conference unavailable\r\n");return 1; }
    q->op=BOARD_CONFERENCE;q->conference=request;
    session_impl_submit(s,q);free(q);return 1;
}

int session_impl_conference_result(Session *s,const OperationResult *r) {
    if(r->page.kind!=OPERATION_PAGE_CONFERENCE)return 0;
    const ConferenceReply *page=&r->page.data.conference;
    static const char *const error[]={
        "", "ERR conference login required\r\n", "ERR conference session expired\r\n",
        "ERR conference access denied\r\n", "ERR conference invalid input\r\n",
        "ERR conference room or message unavailable\r\n", "ERR conference room exists\r\n",
        "ERR conference storage\r\n"
    };
    char text[CONFERENCE_TEXT+180];
    if(page->status!=CONFERENCE_OK)session_impl_emit(s,error[page->status]);
    else switch(page->action) {
    case CONFERENCE_POST:
        snprintf(text,sizeof(text),"CONFERENCE SAVED %u\r\n",page->position);
        session_impl_emit(s,text);break;
    case CONFERENCE_SEEN: case CONFERENCE_POSITION:
        snprintf(text,sizeof(text),"CONFERENCE POSITION %u %u\r\n",page->room,page->position);
        session_impl_emit(s,text);break;
    case CONFERENCE_CREATE:
        snprintf(text,sizeof(text),"CONFERENCE CREATED %u\r\n",page->room);
        session_impl_emit(s,text);break;
    default:
        for(unsigned i=0;i<page->count;i++) {
            if(page->action==CONFERENCE_LIST) {
                const ConferenceRoom *room=&page->rows.rooms[i];
                snprintf(text,sizeof(text),"ROOM %u %s\r\n",room->id,room->title);
            } else {
                const ConferenceLine *line=&page->rows.lines[i];
                snprintf(text,sizeof(text),"LINE %u %u %u %llu %s | %s\r\n",line->id,line->room,line->user,
                         (unsigned long long)line->time,line->name,line->text);
            }
            session_impl_emit(s,text);
        }
        snprintf(text,sizeof(text),"CONFERENCE END NEXT %u THROUGH %u\r\n",page->next,page->position);
        session_impl_emit(s,text);break;
    }
    session_impl_prompt(s);return 1;
}
