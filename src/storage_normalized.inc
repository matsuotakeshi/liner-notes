/* Schema 5 field relations. Parent and child inserts share storage_sync's
 * transaction. Bounds and exact reconstruction reject unsupported encodings. */
typedef struct { const char *name;unsigned at,width; } NormalField;
typedef struct { const char *table,*parent,*events;unsigned bytes,count;const NormalField *fields; } NormalSpec;
#include "storage_normalized_fields.inc"
static const NormalSpec *normal_spec(int kind,int event) {
    for(size_t i=0;i<sizeof(normal_specs)/sizeof(*normal_specs);i++) {
        const NormalSpec *s=&normal_specs[i];
        if(!strcmp(s->parent,row_tables[kind]) && (!*s->events || (event && strchr(s->events,event))))return s;
    }
    return NULL;
}
static int normal_schema(sqlite3 *db) {
    for(size_t i=0;i<sizeof(normal_specs)/sizeof(*normal_specs);i++) {
        const NormalSpec *s=&normal_specs[i];char query[4096];
        int n=snprintf(query,sizeof(query),"CREATE TABLE IF NOT EXISTS %s(stream INTEGER NOT NULL,offset INTEGER NOT NULL",s->table);
        for(unsigned j=0;j<s->count;j++) {
            const NormalField *f=&s->fields[j];
            n+=snprintf(query+n,sizeof(query)-(size_t)n,",%s INTEGER NOT NULL CHECK(typeof(%s)='integer' AND %s BETWEEN 0 AND %s)",f->name,f->name,f->name,f->width==1?"255":f->width==4?"4294967295":"9223372036854775807");
        }
        snprintf(query+n,sizeof(query)-(size_t)n,",PRIMARY KEY(stream,offset),FOREIGN KEY(stream,offset) REFERENCES %s(stream,offset) ON DELETE CASCADE) WITHOUT ROWID;",s->parent);
        if(!sql(db,query))return 0;
        if(*s->events) {
            snprintf(query,sizeof(query),"CREATE TRIGGER IF NOT EXISTS %s_event BEFORE INSERT ON %s WHEN NOT EXISTS(SELECT 1 FROM %s WHERE stream=NEW.stream AND offset=NEW.offset AND instr('%s',char(event_type))>0) BEGIN SELECT RAISE(ABORT,'Wrong normalized event type');END;",s->table,s->table,s->parent,s->events);
            if(!sql(db,query))return 0;
        }
    }
    return sql(db,"CREATE TABLE IF NOT EXISTS member_event_groups(stream INTEGER NOT NULL,offset INTEGER NOT NULL,group_id INTEGER NOT NULL CHECK(typeof(group_id)='integer' AND group_id BETWEEN 1 AND 100),PRIMARY KEY(stream,offset,group_id),FOREIGN KEY(stream,offset) REFERENCES member_records(stream,offset) ON DELETE CASCADE) WITHOUT ROWID;CREATE INDEX IF NOT EXISTS member_event_group_lookup ON member_event_groups(group_id,stream,offset);");
}
static int normal_groups(Store *s,sqlite3_int64 offset,int event,unsigned char *a,int writing) {
    sqlite3_stmt *q=NULL;int maximum=event=='L'?32:event=='G'?100:0;
    const char *query=writing?"INSERT INTO member_event_groups VALUES(?,?,?)":"SELECT group_id FROM member_event_groups WHERE stream=? AND offset=? ORDER BY group_id";
    int ok=sqlite3_prepare_v2(s->db,query,-1,&q,NULL)==SQLITE_OK;
    if(ok && writing) {
        for(int group=1;ok && group<=maximum;group++) {
            unsigned at=event=='L'?12:8+4*(unsigned)((group-1)/32);
            if(!(rget(a+at)&(1u<<((group-1)%32))))continue;
            sqlite3_bind_int64(q,1,s->id);sqlite3_bind_int64(q,2,offset);sqlite3_bind_int(q,3,group);
            ok=sqlite3_step(q)==SQLITE_DONE;sqlite3_reset(q);sqlite3_clear_bindings(q);
        }
    } else if(ok) {
        sqlite3_bind_int64(q,1,s->id);sqlite3_bind_int64(q,2,offset);int rc;
        while((rc=sqlite3_step(q))==SQLITE_ROW) {
            sqlite3_int64 group=sqlite3_column_int64(q,0);
            if(sqlite3_column_type(q,0)!=SQLITE_INTEGER || group<1 || group>maximum) { ok=0;break; }
            unsigned at=event=='L'?12:8+4*(unsigned)((group-1)/32);
            rput(a+at,rget(a+at)|(1u<<((group-1)%32)));
        }
        if(rc!=SQLITE_DONE)ok=0;
    }
    sqlite3_finalize(q);return ok;
}
static int normal_fields(Store *s,sqlite3_int64 offset,int event,unsigned char *a,size_t *bytes,int writing) {
    const NormalSpec *spec=normal_spec(s->kind,event);if(!spec || (writing && *bytes!=spec->bytes))return 0;
    char query[1024];int n=writing?snprintf(query,sizeof(query),"INSERT INTO %s VALUES(?,?",spec->table):snprintf(query,sizeof(query),"SELECT ");
    for(unsigned i=0;i<spec->count;i++) {
        if(writing)n+=snprintf(query+n,sizeof(query)-(size_t)n,",?");
        else n+=snprintf(query+n,sizeof(query)-(size_t)n,"%s%s",i?",":"",spec->fields[i].name);
    }
    if(writing)snprintf(query+n,sizeof(query)-(size_t)n,")");
    else snprintf(query+n,sizeof(query)-(size_t)n," FROM %s WHERE stream=? AND offset=?",spec->table);
    sqlite3_stmt *q=NULL;int ok=sqlite3_prepare_v2(s->db,query,-1,&q,NULL)==SQLITE_OK;
    if(ok) {
        sqlite3_bind_int64(q,1,s->id);sqlite3_bind_int64(q,2,offset);
        if(writing) {
            unsigned char canonical[128]={0};if(s->kind!=1)canonical[0]=(unsigned char)event;
            for(unsigned i=0;ok && i<spec->count;i++) {
                const NormalField *f=&spec->fields[i];uint64_t v=0;
                for(unsigned j=0;j<f->width;j++)v|=(uint64_t)a[f->at+j]<<(8*j);
                if(v>INT64_MAX) { ok=0;break; }
                memcpy(canonical+f->at,a+f->at,f->width);sqlite3_bind_int64(q,(int)i+3,(sqlite3_int64)v);
            }
            if(s->kind==3 && (event=='L'||event=='G')) {
                unsigned at=event=='L'?12:8,width=event=='L'?4:16;
                if(event=='G' && rget(a+20)>15)ok=0;
                memcpy(canonical+at,a+at,width);
            }
            if(memcmp(canonical,a,spec->bytes))ok=0;
            if(ok)ok=sqlite3_step(q)==SQLITE_DONE;
        } else {
            ok=sqlite3_step(q)==SQLITE_ROW;memset(a,0,spec->bytes);if(s->kind!=1)a[0]=(unsigned char)event;
            for(unsigned i=0;ok && i<spec->count;i++) {
                const NormalField *f=&spec->fields[i];sqlite3_int64 v=sqlite3_column_int64(q,(int)i);
                if(sqlite3_column_type(q,(int)i)!=SQLITE_INTEGER || v<0 || (f->width<8 && (uint64_t)v>=((uint64_t)1<<(8*f->width)))) { ok=0;break; }
                for(unsigned j=0;j<f->width;j++)a[f->at+j]=(unsigned char)((uint64_t)v>>(8*j));
            }
        }
    }
    sqlite3_finalize(q);
    if(ok && s->kind==3)ok=normal_groups(s,offset,event,a,writing);
    *bytes=spec->bytes;return ok;
}
