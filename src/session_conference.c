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

static int topic_submit(Session *s,const ConferenceRequest *request) {
    OperationRequest *q=calloc(1,sizeof(*q));
    if(!q)return 0;
    q->op=BOARD_CONFERENCE;q->conference=*request;
    int accepted=session_submit(s,q)==SESSION_OPERATION_ACCEPTED;
    free(q);return accepted;
}

int session_topic_command(Session *s,char *line) {
    char command[6];size_t n=strcspn(line," ");
    if(n!=5)return 0;
    for(size_t i=0;i<n;i++)command[i]=line[i]>='a' && line[i]<='z'?(char)(line[i]-'a'+'A'):line[i];
    command[n]=0;
    if(strcmp(command,"TOPIC"))return 0;
    const char *args=line+n+strspn(line+n," ");
    ConferenceRequest q={.action=TOPIC_UNREAD};
    if(*args>='0' && *args<='9') {
        if(!number(&args,&q.cursor,1) || *args)goto invalid;
        if(!topic_submit(s,&q))session_impl_queue_text(s,"ERR topic unavailable\r\n");
        return 1;
    }
    char verb[4];n=strcspn(args," ");
    if(n>=sizeof(verb))goto invalid;
    for(size_t i=0;i<n;i++)verb[i]=args[i]>='a' && args[i]<='z'?(char)(args[i]-'a'+'A'):args[i];
    verb[n]=0;args+=n;args+=strspn(args," ");
    if(!*verb || !strcmp(verb,"ALL")) {
        if(*verb)q.action=TOPIC_HISTORY;
        if(*args && (!number(&args,&q.cursor,1) || *args))goto invalid;
    } else if(!strcmp(verb,"-W")) {
        if(!*args || strlen(args)>CONFERENCE_TEXT)goto invalid;
        q.action=TOPIC_POST;strcpy(q.text,args);
    } else if(!strcmp(verb,"-D")) {
        if(!number(&args,&q.cursor,0) || *args)goto invalid;
        q.action=TOPIC_DELETE;
    } else goto invalid;
    if(!topic_submit(s,&q))session_impl_queue_text(s,"ERR topic unavailable\r\n");
    return 1;
invalid:
    session_impl_queue_text(s,"TOPIC [cursor | ALL [cursor] | -W text | -D id]\r\n");
    return 1;
}

int session_topic_result(Session *s,const OperationResult *r) {
    if(r->page.kind!=OPERATION_PAGE_CONFERENCE)return 0;
    const ConferenceReply *p=&r->page.data.conference;
    if(p->action<TOPIC_HISTORY || p->action>TOPIC_SEEN)return 0;
    int ja=session_ui_snapshot(s).language==LANGUAGE_JA;
    if(p->status!=CONFERENCE_OK)session_impl_queue_text(s,session_impl_conference_error(p->status));
    else if(p->action==TOPIC_POST)session_impl_queue_text(s,ja?"伝言を書き込みました。\r\n":"Topic posted.\r\n");
    else if(p->action==TOPIC_DELETE)session_impl_queue_text(s,ja?"伝言を削除しました。\r\n":"Topic deleted.\r\n");
    else if(p->action!=TOPIC_SEEN) {
        char line[CONFERENCE_TEXT+100];
        for(unsigned i=0;i<p->count;i++) {
            const ConferenceLine *row=&p->rows.lines[i];
            snprintf(line,sizeof(line),"#%u %s | %s\r\n",row->id,row->name,row->text);
            session_impl_queue_text(s,line);
        }
        if(p->next) {
            snprintf(line,sizeof(line),"TOPIC%s %u\r\n",p->action==TOPIC_HISTORY?" ALL":"",p->next);
            session_impl_queue_text(s,line);
        }
        if(p->action==TOPIC_UNREAD && p->count && s->core.active && !s->core.closing) {
            ConferenceRequest mark={.action=TOPIC_SEEN,.cursor=p->position};
            if(topic_submit(s,&mark))return 1;
            session_impl_queue_text(s,"ERR topic read position\r\n");
        }
    }
    session_impl_prompt(s);return 1;
}

const char *session_impl_conference_error(ConferenceStatus status) {
    static const char *const error[]={
        "", "ERR conference login required\r\n", "ERR conference session expired\r\n",
        "ERR conference access denied\r\n", "ERR conference invalid input\r\n",
        "ERR conference room or message unavailable\r\n", "ERR conference room exists\r\n",
        "ERR conference storage\r\n"
    };
    return (unsigned)status<sizeof(error)/sizeof(*error)?error[status]:error[CONFERENCE_STORAGE];
}

int session_impl_conference_result(Session *s,const OperationResult *r) {
    if(session_topic_result(s,r))return 1;
    if(r->page.kind!=OPERATION_PAGE_CONFERENCE)return 0;
    const ConferenceReply *page=&r->page.data.conference;
    char text[CONFERENCE_TEXT+180];
    if(page->status!=CONFERENCE_OK)session_impl_emit(s,session_impl_conference_error(page->status));
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
