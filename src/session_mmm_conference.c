#include "session_mmm_internal.h"
#include "mmm_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mmm_conference_menu(Session *s) {
    if(mmm_view(s)->conference_room)
        mmm_say(s,"Enter text to post. /r: history  /n: unread  /a: mark read  /l: rooms  /q: exit\r\n",
                  "本文を入力して発言。/r 履歴  /n 未読  /a 既読にする  /l 部屋一覧  /q 終了\r\n");
    else mmm_say(s,"Number or e: enter  r: rooms  n: unread scan  a: mark read  q: exit\r\n",
                   "番号・e 入室  r 部屋一覧  n 未読巡回  a 既読にする  q 終了\r\n");
}

static int submit(Session *s,ConferenceAction action,uint32_t room,uint32_t cursor,const char *text) {
    OperationRequest *q=mmm_request(s,BOARD_CONFERENCE);
    if(!q)return 0;
    q->conference.action=action;q->conference.room=room;q->conference.cursor=cursor;
    if(text) {
        size_t size=strlen(text);
        if(size>CONFERENCE_TEXT) {
            mmm_say(s,"Conference line exceeds 512 bytes.\r\n","発言は512バイト以内です。\r\n");
            /* mmm_submit owns the request only once submitted. */
            free(q);return 0;
        }
        memcpy(q->conference.text,text,size+1);
    }
    return mmm_submit(s,q,JOB_CONFERENCE);
}

static void read_page(Session *s,ConferenceAction action,uint32_t scope,int automatic) {
    MmmUi *m=mmm_state(s);
    m->conference_mark=m->conference_next=0;
    m->conference_action=action;m->conference_scope=scope;
    int accepted=submit(s,action,scope,0,NULL);
    m->conference_automatic=automatic && accepted;
}

void mmm_conference_enter(Session *s,const char *arg) {
    if(*arg && strcmp(arg,"-n") && strcmp(arg,"-c")) { mmm_bad_service_command(s);return; }
    MmmUi *m=mmm_state(s);
    m->screen=TALK_SCREEN;m->conference_room=0;
    if(!strcmp(arg,"-n"))read_page(s,CONFERENCE_UNREAD,0,1);
    else {
        mmm_conference_menu(s);
        if(!*arg)read_page(s,CONFERENCE_LIST,0,0);
    }
}

static char *verb(char *line) {
    while(*line && *line!=' ' && *line!='\t') {
        if(*line>='A' && *line<='Z')*line=(char)(*line+'a'-'A');
        line++;
    }
    if(*line)*line++=0;
    while(*line==' ' || *line=='\t')line++;
    return line;
}

static void leave_room(Session *s,int exit) {
    MmmUi *m=mmm_state(s);
    m->conference_room=m->conference_mark=m->conference_next=0;
    m->conference_automatic=0;
    m->screen=exit?MAIN_SCREEN:TALK_SCREEN;
    mmm_menu(s,1);
}

int mmm_conference_line(Session *s,char *line) {
    MmmUi *m=mmm_state(s);
    if(m->conference_room && *line && *line!='/' && *line!='\\') {
        if(m->conference_automatic)mmm_say(s,"Wait for the unread scan.\r\n","未読巡回の終了を待ってください。\r\n");
        else submit(s,CONFERENCE_POST,m->conference_room,0,line);
        return 1;
    }
    if(*line=='/' || *line=='\\')line++;
    char *arg=verb(line);
    uint32_t room;
    if(!*arg && (!strcmp(line,"q") || !strcmp(line,"quit") || !strcmp(line,".")))leave_room(s,1);
    else if(!*arg && m->conference_room && (!strcmp(line,"l") || !strcmp(line,"leave")))leave_room(s,0);
    else if(m->conference_automatic)mmm_say(s,"Unread scan in progress.\r\n","未読巡回中です。\r\n");
    else if(!*arg && (!*line || !strcmp(line,"?")))mmm_conference_menu(s);
    else if(!*arg && (!strcmp(line,"r") || !strcmp(line,"report") || !strcmp(line,"ra") || !strcmp(line,"scan")))
        read_page(s,m->conference_room?CONFERENCE_HISTORY:CONFERENCE_LIST,m->conference_room,0);
    else if(!*arg && (!strcmp(line,"n") || !strcmp(line,"new")))read_page(s,CONFERENCE_UNREAD,m->conference_room,1);
    else if(!*arg && !strcmp(line,"more") && m->conference_next)
        submit(s,m->conference_action,m->conference_scope,m->conference_next,NULL);
    else if(!*arg && !strcmp(line,"a") && m->conference_mark)
        submit(s,CONFERENCE_SEEN,m->conference_scope,m->conference_mark,NULL);
    else if(!*arg && !strcmp(line,"position") && m->conference_room)
        submit(s,CONFERENCE_POSITION,m->conference_room,0,NULL);
    else if(!m->conference_room &&
        ((!*arg && mmm_number(line,&room)) ||
         ((!strcmp(line,"e") || !strcmp(line,"enter") || !strcmp(line,"look")) && mmm_number(arg,&room)))) {
        m->conference_room=room;
        read_page(s,CONFERENCE_HISTORY,room,0);
    } else mmm_bad_service_command(s);
    return 1;
}

void mmm_conference_result(Session *s,const ConferenceReply *page) {
    MmmUi *m=mmm_state(s);
    char text[CONFERENCE_TEXT+170];
    if(page->status!=CONFERENCE_OK) {
        m->conference_automatic=0;
        if(page->action==CONFERENCE_HISTORY && (page->status==CONFERENCE_MISSING || page->status==CONFERENCE_INVALID))
            m->conference_room=0;
        if(page->status==CONFERENCE_LOGIN || page->status==CONFERENCE_EXPIRED || page->status==CONFERENCE_DENIED)
            mmm_say(s,"Conference access denied.\r\n","一行会議の利用権限がありません。\r\n");
        else if(page->status==CONFERENCE_MISSING)
            mmm_say(s,"Conference room or line not found.\r\n","部屋または発言が見つかりません。\r\n");
        else if(page->status==CONFERENCE_INVALID)
            mmm_say(s,"Invalid conference input.\r\n","一行会議の入力が不正です。\r\n");
        else mmm_say(s,"Conference storage operation failed.\r\n","一行会議の保存領域を処理できませんでした。\r\n");
        return;
    }
    switch(page->action) {
    case CONFERENCE_POST:
        snprintf(text,sizeof(text),mmm_local(s,"Posted #%u.\r\n","発言 #%u を登録しました。\r\n"),page->position);
        session_impl_queue_text(s,text);break;
    case CONFERENCE_SEEN: case CONFERENCE_POSITION:
        snprintf(text,sizeof(text),mmm_local(s,"Read through #%u.\r\n","既読位置 #%u\r\n"),page->position);
        session_impl_queue_text(s,text);
        if(page->action==CONFERENCE_SEEN)m->conference_mark=0;
        break;
    case CONFERENCE_LIST:
        m->conference_next=page->next;
        for(unsigned i=0;i<page->count;i++) {
            const ConferenceRoom *row=&page->rows.rooms[i];
            snprintf(text,sizeof(text),"%u  %s\r\n",row->id,row->title);session_impl_queue_text(s,text);
        }
        if(page->next)mmm_say(s,"more: next rooms\r\n","more: 続きの部屋\r\n");
        break;
    default:
        m->conference_next=page->next;
        for(unsigned i=0;i<page->count;i++) {
            const ConferenceLine *row=&page->rows.lines[i];
            snprintf(text,sizeof(text),"[%u #%u %llu %s] %s\r\n",row->room,row->id,
                     (unsigned long long)row->time,row->name,row->text);
            session_impl_queue_text(s,text);
        }
        if(page->action==CONFERENCE_UNREAD && page->count)m->conference_mark=page->position;
        if(!m->conference_automatic && page->next)mmm_say(s,"more: next lines\r\n","more: 続きの発言\r\n");
        if(!page->count && !m->conference_mark)mmm_say(s,"No conference lines.\r\n","発言はありません。\r\n");
        break;
    }
}

void mmm_conference_tick(Session *s) {
    MmmUi *m=mmm_state(s);
    if(m->conference_next && submit(s,CONFERENCE_UNREAD,m->conference_scope,m->conference_next,NULL))return;
    m->conference_automatic=0;
    if(m->conference_mark)
        mmm_say(s,"Use A to mark the displayed lines read.\r\n","A で表示した発言を既読にできます。\r\n");
    mmm_prompt(s);
}
