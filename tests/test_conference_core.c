#include "conference_core.h"
#include <assert.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

static ConferenceStatus identity(void *context,const ConferenceActor *actor,ConferenceIdentity *out) {
    (void)context;
    if(actor->session!=(unsigned long)actor->user+40 || actor->slot!=2 || actor->epoch!=3 || actor->user==4)
        return CONFERENCE_EXPIRED;
    strcpy(out->name,actor->user==1?"Alice":"Bob");
    out->administrator=actor->user==1;
    out->guest=actor->user==3;
    return CONFERENCE_OK;
}

static ConferenceReply request(sqlite3 *db,uint32_t user,ConferenceAction action,uint32_t room,uint32_t cursor,
                               const char *text,ConferenceStatus expected) {
    ConferenceRequest q={.action=action,.room=room,.cursor=cursor};
    if(text)strcpy(q.text,text);
    ConferenceActor actor={user+40,2,user,3};
    ConferenceReply out;
    assert(conference_run(db,identity,NULL,&actor,&q,&out)==expected);
    assert(out.status==expected && out.action==action);
    if(expected!=CONFERENCE_OK)assert(!out.count && !out.position && !out.next);
    return out;
}

int main(void) {
    sqlite3 *db=NULL;
    assert(sqlite3_open(":memory:",&db)==SQLITE_OK);
    assert(sqlite3_exec(db,"PRAGMA foreign_keys=ON",NULL,NULL,NULL)==SQLITE_OK);
    assert(conference_schema(db) && conference_schema(db));
    assert(request(db,1,CONFERENCE_LIST,0,0,NULL,CONFERENCE_OK).count==1);
    request(db,0,CONFERENCE_LIST,0,0,NULL,CONFERENCE_LOGIN);
    request(db,3,CONFERENCE_UNREAD,0,0,NULL,CONFERENCE_DENIED);
    request(db,3,CONFERENCE_POST,1,0,"guest",CONFERENCE_DENIED);
    request(db,4,CONFERENCE_POST,1,0,"inactive",CONFERENCE_EXPIRED);
    request(db,2,CONFERENCE_CREATE,2,0,"Another room",CONFERENCE_DENIED);
    request(db,1,CONFERENCE_CREATE,2,0,"Another room",CONFERENCE_OK);
    request(db,1,CONFERENCE_CREATE,2,0,"Changed",CONFERENCE_EXISTS);
    request(db,1,CONFERENCE_POST,1501,0,"bad room",CONFERENCE_INVALID);
    request(db,1,CONFERENCE_POST,3,0,"absent room",CONFERENCE_MISSING);
    request(db,1,CONFERENCE_POST,1,0,"",CONFERENCE_INVALID);
    request(db,1,CONFERENCE_POST,1,0,"   ",CONFERENCE_INVALID);
    request(db,1,CONFERENCE_POST,1,0,"a\x1b B",CONFERENCE_INVALID);
    request(db,1,CONFERENCE_POST,1,0,"\xc0\xaf",CONFERENCE_INVALID);
    assert(request(db,1,CONFERENCE_POST,1,0,"こんにちは",CONFERENCE_OK).position==1);
    assert(request(db,2,CONFERENCE_POST,2,0,"Other room",CONFERENCE_OK).position==2);
    for(unsigned i=3;i<=11;i++)
        assert(request(db,2,CONFERENCE_POST,1,0,"preserved",CONFERENCE_OK).position==i);
    ConferenceReply first=request(db,1,CONFERENCE_UNREAD,0,0,NULL,CONFERENCE_OK);
    assert(first.count==CONFERENCE_PAGE && first.next==8 && first.position==8 &&
           first.rows.lines[0].user==1 && !strcmp(first.rows.lines[0].name,"Alice"));
    assert(request(db,1,CONFERENCE_UNREAD,0,first.next,NULL,CONFERENCE_OK).count==3);
    assert(request(db,1,CONFERENCE_POSITION,1,0,NULL,CONFERENCE_OK).position==0);
    request(db,1,CONFERENCE_SEEN,1,2,NULL,CONFERENCE_MISSING);
    assert(request(db,1,CONFERENCE_SEEN,0,8,NULL,CONFERENCE_OK).position==8);
    assert(request(db,1,CONFERENCE_POSITION,2,0,NULL,CONFERENCE_OK).position==2);
    assert(request(db,1,CONFERENCE_SEEN,1,1,NULL,CONFERENCE_OK).position==8);
    assert(request(db,2,CONFERENCE_POSITION,1,0,NULL,CONFERENCE_OK).position==0);
    assert(request(db,2,CONFERENCE_POSITION,2,0,NULL,CONFERENCE_OK).position==0);
    assert(request(db,1,CONFERENCE_UNREAD,0,0,NULL,CONFERENCE_OK).count==3);
    assert(request(db,1,CONFERENCE_HISTORY,2,0,NULL,CONFERENCE_OK).count==1);
    assert(sqlite3_exec(db,"PRAGMA query_only=ON",NULL,NULL,NULL)==SQLITE_OK);
    request(db,2,CONFERENCE_POST,1,0,"must not be saved",CONFERENCE_STORAGE);
    request(db,2,CONFERENCE_SEEN,1,11,NULL,CONFERENCE_STORAGE);
    assert(request(db,2,CONFERENCE_POSITION,1,0,NULL,CONFERENCE_OK).position==0);

    ConferenceActor forged={41,2,2,3};
    ConferenceRequest denied={.action=CONFERENCE_UNREAD};
    ConferenceReply unread;
    assert(conference_run(db,identity,NULL,&forged,&denied,&unread)==CONFERENCE_EXPIRED && !unread.count);
    memset(denied.text,'x',sizeof(denied.text));
    denied.action=CONFERENCE_POST;denied.room=1;
    ConferenceActor actor={41,2,1,3};
    assert(conference_run(db,identity,NULL,&actor,&denied,&unread)==CONFERENCE_INVALID);
    assert(sqlite3_exec(db,"PRAGMA query_only=OFF",NULL,NULL,NULL)==SQLITE_OK);
    denied.text[CONFERENCE_TEXT]=0;
    assert(conference_run(db,identity,NULL,&actor,&denied,&unread)==CONFERENCE_OK && unread.position==12);
    request(db,3,TOPIC_HISTORY,0,0,NULL,CONFERENCE_DENIED);
    request(db,4,TOPIC_POST,0,0,"revoked",CONFERENCE_EXPIRED);
    request(db,2,TOPIC_POST,0,0,"\x1b",CONFERENCE_INVALID);
    assert(!request(db,2,TOPIC_HISTORY,0,0,NULL,CONFERENCE_OK).count);
    assert(request(db,2,TOPIC_POST,0,0,"First topic",CONFERENCE_OK).position==1);
    assert(request(db,2,TOPIC_POST,0,0,"Second topic",CONFERENCE_OK).position==2);
    request(db,2,TOPIC_SEEN,0,2,NULL,CONFERENCE_OK);
    request(db,2,TOPIC_SEEN,0,1,NULL,CONFERENCE_OK);
    assert(!request(db,2,TOPIC_UNREAD,0,0,NULL,CONFERENCE_OK).count);
    assert(request(db,1,TOPIC_UNREAD,0,0,NULL,CONFERENCE_OK).count==2);
    assert(request(db,1,TOPIC_REVERSE,0,0,NULL,CONFERENCE_OK).rows.lines[0].id==2);
    assert(request(db,1,TOPIC_POST,0,0,"Administrator topic",CONFERENCE_OK).position==3);
    request(db,2,TOPIC_DELETE,0,3,NULL,CONFERENCE_MISSING);
    request(db,1,TOPIC_DELETE,0,2,NULL,CONFERENCE_OK);
    request(db,2,TOPIC_DELETE,0,1,NULL,CONFERENCE_OK);
    assert(request(db,2,TOPIC_UNREAD,0,0,NULL,CONFERENCE_OK).rows.lines[0].id==3);
    assert(request(db,2,TOPIC_POST,0,0,"Fourth topic",CONFERENCE_OK).position==4);
    assert(request(db,1,CONFERENCE_POSITION,1,0,NULL,CONFERENCE_OK).position==8);
    assert(sqlite3_exec(db,"PRAGMA query_only=ON",NULL,NULL,NULL)==SQLITE_OK);
    request(db,1,TOPIC_DELETE,0,3,NULL,CONFERENCE_STORAGE);
    assert(request(db,1,TOPIC_HISTORY,0,0,NULL,CONFERENCE_OK).count==2);
    assert(sqlite3_close(db)==SQLITE_OK);
    puts("PASS line conference: access, deterministic pagination, independent and monotonic markers, invalid text and write failures");
    return 0;
}
