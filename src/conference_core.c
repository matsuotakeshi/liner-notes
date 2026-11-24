#include "conference_core.h"
#include "chat_core.h"
#include <sqlite3.h>
#include <string.h>
#include <time.h>

int conference_schema(sqlite3 *db) {
    const char *sql=
        "CREATE TABLE IF NOT EXISTS conference_rooms(id INTEGER PRIMARY KEY CHECK(id BETWEEN 1 AND 1500),"
        " title TEXT NOT NULL CHECK(length(CAST(title AS BLOB)) BETWEEN 1 AND 80));"
        "CREATE TABLE IF NOT EXISTS conference_lines(id INTEGER PRIMARY KEY AUTOINCREMENT CHECK(id BETWEEN 1 AND 2147483646),"
        " room INTEGER NOT NULL REFERENCES conference_rooms(id), user INTEGER NOT NULL CHECK(user>0),"
        " name TEXT NOT NULL CHECK(length(CAST(name AS BLOB)) BETWEEN 1 AND 80), stamp INTEGER NOT NULL CHECK(stamp>=0),"
        " body TEXT NOT NULL CHECK(length(CAST(body AS BLOB)) BETWEEN 1 AND 512), UNIQUE(room,id));"
        "CREATE TABLE IF NOT EXISTS conference_read(user INTEGER NOT NULL CHECK(user>0),"
        " room INTEGER NOT NULL REFERENCES conference_rooms(id),"
        " through INTEGER NOT NULL, PRIMARY KEY(user,room),"
        " FOREIGN KEY(room,through) REFERENCES conference_lines(room,id));"
        "INSERT OR IGNORE INTO conference_rooms VALUES(1,'General');";
    return sqlite3_exec(db,sql,NULL,NULL,NULL)==SQLITE_OK;
}

static sqlite3_stmt *statement(sqlite3 *db,const char *sql) {
    sqlite3_stmt *s=NULL;
    if(sqlite3_prepare_v2(db,sql,-1,&s,NULL)!=SQLITE_OK) { sqlite3_finalize(s);return NULL; }
    return s;
}

static int safe_text(const char *text,size_t limit) {
    if(!memchr(text,0,limit+1) || !chat_core_text_valid(text))return 0;
    return text[strspn(text," ")]!=0;
}

static int copy_text(sqlite3_stmt *s,int index,char *out,size_t size) {
    const char *text=(const char *)sqlite3_column_text(s,index);
    int n=sqlite3_column_bytes(s,index);
    if(!text || n<1 || (size_t)n>=size || strlen(text)!=(size_t)n ||
       !safe_text(text,(size_t)n))return 0;
    memcpy(out,text,(size_t)n+1);return 1;
}

static ConferenceStatus rooms(sqlite3 *db,const ConferenceRequest *q,ConferenceReply *reply) {
    sqlite3_stmt *s=statement(db,"SELECT id,title FROM conference_rooms WHERE id>? ORDER BY id LIMIT 9");
    if(!s)return CONFERENCE_STORAGE;
    int rc=sqlite3_bind_int64(s,1,q->cursor);
    if(rc==SQLITE_OK)while((rc=sqlite3_step(s))==SQLITE_ROW) {
        if(reply->count==CONFERENCE_PAGE) { reply->next=reply->rows.rooms[reply->count-1].id;break; }
        ConferenceRoom *row=&reply->rows.rooms[reply->count];
        row->id=(uint32_t)sqlite3_column_int64(s,0);
        if(!copy_text(s,1,row->title,sizeof(row->title))) { rc=SQLITE_CORRUPT;break; }
        reply->count++;
    }
    return sqlite3_finalize(s)==SQLITE_OK && (rc==SQLITE_ROW || rc==SQLITE_DONE)?CONFERENCE_OK:CONFERENCE_STORAGE;
}

static ConferenceStatus room_exists(sqlite3 *db,uint32_t room) {
    sqlite3_stmt *s=statement(db,"SELECT id FROM conference_rooms WHERE id=?");
    if(!s)return CONFERENCE_STORAGE;
    int rc=sqlite3_bind_int64(s,1,room);
    if(rc==SQLITE_OK)rc=sqlite3_step(s);
    if(sqlite3_finalize(s)!=SQLITE_OK)return CONFERENCE_STORAGE;
    return rc==SQLITE_ROW?CONFERENCE_OK:rc==SQLITE_DONE?CONFERENCE_MISSING:CONFERENCE_STORAGE;
}

static ConferenceStatus lines(sqlite3 *db,uint32_t user,const ConferenceRequest *q,ConferenceReply *reply) {
    sqlite3_stmt *s=statement(db,
        "SELECT l.id,l.room,l.user,l.stamp,l.name,l.body FROM conference_lines l"
        " LEFT JOIN conference_read r ON r.user=?1 AND r.room=l.room"
        " WHERE l.id>?2 AND (?3=0 OR l.room=?3) AND (?4=0 OR l.id>coalesce(r.through,0))"
        " ORDER BY l.id LIMIT 9");
    if(!s)return CONFERENCE_STORAGE;
    int rc=sqlite3_bind_int64(s,1,user);
    if(rc==SQLITE_OK)rc=sqlite3_bind_int64(s,2,q->cursor);
    if(rc==SQLITE_OK)rc=sqlite3_bind_int64(s,3,q->room);
    if(rc==SQLITE_OK)rc=sqlite3_bind_int(s,4,q->action==CONFERENCE_UNREAD);
    if(rc==SQLITE_OK)while((rc=sqlite3_step(s))==SQLITE_ROW) {
        if(reply->count==CONFERENCE_PAGE) { reply->next=reply->position;break; }
        ConferenceLine *row=&reply->rows.lines[reply->count];
        row->id=(uint32_t)sqlite3_column_int64(s,0);row->room=(uint32_t)sqlite3_column_int64(s,1);
        row->user=(uint32_t)sqlite3_column_int64(s,2);row->time=(uint64_t)sqlite3_column_int64(s,3);
        if(!copy_text(s,4,row->name,sizeof(row->name)) || !copy_text(s,5,row->text,sizeof(row->text))) {
            rc=SQLITE_CORRUPT;break;
        }
        reply->position=row->id;reply->count++;
    }
    return sqlite3_finalize(s)==SQLITE_OK && (rc==SQLITE_ROW || rc==SQLITE_DONE)?CONFERENCE_OK:CONFERENCE_STORAGE;
}

static ConferenceStatus post(sqlite3 *db,uint32_t user,const ConferenceIdentity *who,
                             const ConferenceRequest *q,ConferenceReply *reply) {
    if(!safe_text(q->text,CONFERENCE_TEXT))return CONFERENCE_INVALID;
    time_t stamp=time(NULL);
    if(stamp<0)return CONFERENCE_STORAGE;
    sqlite3_stmt *s=statement(db,"INSERT INTO conference_lines(room,user,name,stamp,body) VALUES(?,?,?,?,?)");
    if(!s)return CONFERENCE_STORAGE;
    int ok=sqlite3_bind_int64(s,1,q->room)==SQLITE_OK && sqlite3_bind_int64(s,2,user)==SQLITE_OK &&
        sqlite3_bind_text(s,3,who->name,-1,SQLITE_STATIC)==SQLITE_OK &&
        sqlite3_bind_int64(s,4,(sqlite3_int64)stamp)==SQLITE_OK &&
        sqlite3_bind_text(s,5,q->text,-1,SQLITE_STATIC)==SQLITE_OK && sqlite3_step(s)==SQLITE_DONE;
    if(sqlite3_finalize(s)!=SQLITE_OK || !ok)return CONFERENCE_STORAGE;
    reply->position=(uint32_t)sqlite3_last_insert_rowid(db);
    return CONFERENCE_OK;
}

static ConferenceStatus position(sqlite3 *db,uint32_t user,uint32_t room,ConferenceReply *reply) {
    sqlite3_stmt *s=statement(db,"SELECT through FROM conference_read WHERE user=? AND room=?");
    if(!s)return CONFERENCE_STORAGE;
    int rc=sqlite3_bind_int64(s,1,user)==SQLITE_OK && sqlite3_bind_int64(s,2,room)==SQLITE_OK?sqlite3_step(s):SQLITE_ERROR;
    if(rc==SQLITE_ROW)reply->position=(uint32_t)sqlite3_column_int64(s,0);
    return sqlite3_finalize(s)==SQLITE_OK && (rc==SQLITE_ROW || rc==SQLITE_DONE)?CONFERENCE_OK:CONFERENCE_STORAGE;
}

static ConferenceStatus seen(sqlite3 *db,uint32_t user,const ConferenceRequest *q,ConferenceReply *reply) {
    if(!q->cursor)return CONFERENCE_INVALID;
    sqlite3_stmt *s=statement(db,"SELECT 1 FROM conference_lines WHERE id=?1 AND (?2=0 OR room=?2)");
    if(!s)return CONFERENCE_STORAGE;
    int rc=sqlite3_bind_int64(s,1,q->cursor)==SQLITE_OK && sqlite3_bind_int64(s,2,q->room)==SQLITE_OK?sqlite3_step(s):SQLITE_ERROR;
    if(sqlite3_finalize(s)!=SQLITE_OK)return CONFERENCE_STORAGE;
    if(rc!=SQLITE_ROW)return rc==SQLITE_DONE?CONFERENCE_MISSING:CONFERENCE_STORAGE;

    /* A single statement marks the delivered interval across every requested room.
     * An older connection never rolls back a more recent marker. */
    s=statement(db,"INSERT INTO conference_read(user,room,through)"
        " SELECT ?1,room,max(id) FROM conference_lines WHERE id<=?2 AND (?3=0 OR room=?3) GROUP BY room"
        " ON CONFLICT(user,room) DO UPDATE SET through=max(through,excluded.through)");
    if(!s)return CONFERENCE_STORAGE;
    int ok=sqlite3_bind_int64(s,1,user)==SQLITE_OK && sqlite3_bind_int64(s,2,q->cursor)==SQLITE_OK &&
           sqlite3_bind_int64(s,3,q->room)==SQLITE_OK && sqlite3_step(s)==SQLITE_DONE;
    if(sqlite3_finalize(s)!=SQLITE_OK || !ok)return CONFERENCE_STORAGE;
    if(q->room)return position(db,user,q->room,reply);
    reply->position=q->cursor;return CONFERENCE_OK;
}

static ConferenceStatus create(sqlite3 *db,const ConferenceIdentity *who,const ConferenceRequest *q) {
    if(!who->administrator)return CONFERENCE_DENIED;
    if(!safe_text(q->text,80))return CONFERENCE_INVALID;
    sqlite3_stmt *s=statement(db,"INSERT INTO conference_rooms(id,title) VALUES(?,?) ON CONFLICT(id) DO NOTHING");
    if(!s)return CONFERENCE_STORAGE;
    int ok=sqlite3_bind_int64(s,1,q->room)==SQLITE_OK && sqlite3_bind_text(s,2,q->text,-1,SQLITE_STATIC)==SQLITE_OK &&
        sqlite3_step(s)==SQLITE_DONE;
    if(sqlite3_finalize(s)!=SQLITE_OK || !ok)return CONFERENCE_STORAGE;
    return sqlite3_changes(db)?CONFERENCE_OK:CONFERENCE_EXISTS;
}

static ConferenceStatus run(sqlite3 *db,ConferenceResolve resolve,void *context,const ConferenceActor *actor,
                            const ConferenceRequest *q,ConferenceReply *reply) {
    if(!actor || !actor->user)return CONFERENCE_LOGIN;
    if(!actor->session || actor->slot<0 || !resolve)return CONFERENCE_EXPIRED;
    ConferenceIdentity who={0};
    ConferenceStatus status=resolve(context,actor,&who);
    if(status!=CONFERENCE_OK)return status;
    if(who.guest)return CONFERENCE_DENIED;
    if(!db)return CONFERENCE_STORAGE;
    if(!q || q->action<CONFERENCE_LIST || q->action>CONFERENCE_CREATE || q->room>CONFERENCE_ROOMS ||
       q->cursor>CONFERENCE_LAST_ID ||
       (!q->room && q->action!=CONFERENCE_LIST && q->action!=CONFERENCE_UNREAD && q->action!=CONFERENCE_SEEN))
        return CONFERENCE_INVALID;
    reply->room=q->room;
    if(q->action==CONFERENCE_LIST)return rooms(db,q,reply);
    if(q->action==CONFERENCE_CREATE)return create(db,&who,q);
    if(q->room) {
        status=room_exists(db,q->room);
        if(status!=CONFERENCE_OK)return status;
    }
    switch(q->action) {
    case CONFERENCE_HISTORY: case CONFERENCE_UNREAD: return lines(db,actor->user,q,reply);
    case CONFERENCE_POSITION: return position(db,actor->user,q->room,reply);
    case CONFERENCE_SEEN: return seen(db,actor->user,q,reply);
    case CONFERENCE_POST:
        return safe_text(who.name,80)?post(db,actor->user,&who,q,reply):CONFERENCE_STORAGE;
    default: return CONFERENCE_INVALID;
    }
}

ConferenceStatus conference_run(sqlite3 *db,ConferenceResolve resolve,void *context,const ConferenceActor *actor,
                                const ConferenceRequest *q,ConferenceReply *reply) {
    memset(reply,0,sizeof(*reply));
    ConferenceStatus status=run(db,resolve,context,actor,q,reply);
    if(status!=CONFERENCE_OK)memset(reply,0,sizeof(*reply));
    if(q)reply->action=q->action;
    reply->status=status;
    return status;
}
