/* Schema 2: authoritative domain rows. Encoded record headers retain ancillary
 * event attributes; title/body/identity/credentials live only in named columns.
 * This adapter keeps the established validators and offline compaction format. */
#define ROW_MAX (262144+256)
static int structured_requested,storage_version;
static const char *row_tables[]={"","article_records","mail_records","member_records",
    "profile_events","board_access_events","download_events","userlog_events","chat_events",
    "activity_events","board_policy_events","catalog_events","sigop_events","board_move_events","userlog_lock_events","moderation_events","content_events"};
static const char *event_magics[]={"LNPROF01","LNBACC01","LNDOWN01","LNUSER01","LNCHAT01","LNACTV01","LNBDP001","LNCAT001","LNSIG001","LNBMOV01","LNULOCK1","RTMOD001","RTEXTRA1"};
static uint32_t event_sum(const unsigned char *r,size_t n) { uint32_t h=2166136261u;for(size_t i=0;i<n-4;i++)h=(h^r[i])*16777619u;return h; }

static uint32_t rget(const unsigned char *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void rput(unsigned char *p,uint32_t n) { for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(8*i)); }
static int row_schema(sqlite3 *db) {
    char schema[512];snprintf(schema,sizeof(schema),"CREATE TABLE IF NOT EXISTS record_streams(stream INTEGER PRIMARY KEY REFERENCES streams(id) ON DELETE CASCADE,kind INTEGER NOT NULL CHECK(kind BETWEEN 1 AND %u),magic BLOB NOT NULL CHECK(length(magic)=8));",storage_version>=3?16:3);
    if(!sql(db,schema))return 0;
    if(storage_version>=3)for(unsigned i=4;i<17;i++) {
        char query[2048];snprintf(query,sizeof(query),
            "CREATE TABLE IF NOT EXISTS %s(stream INTEGER NOT NULL REFERENCES streams(id) ON DELETE CASCADE,offset INTEGER NOT NULL,event_type INTEGER NOT NULL,sequence INTEGER NOT NULL,actor_id INTEGER NOT NULL,target_id INTEGER NOT NULL,created_at INTEGER NOT NULL,text TEXT NOT NULL,checksum INTEGER NOT NULL,flags INTEGER NOT NULL,context_id INTEGER NOT NULL,PRIMARY KEY(stream,offset)) WITHOUT ROWID;"
            "CREATE INDEX IF NOT EXISTS %s_target ON %s(stream,target_id,created_at);"
            "CREATE INDEX IF NOT EXISTS %s_actor ON %s(stream,actor_id,created_at);",
            row_tables[i],row_tables[i],row_tables[i],row_tables[i],row_tables[i]);
        if(!sql(db,query))return 0;
    }
    for(unsigned i=1;i<=3;i++) {
        char query[4096];snprintf(query,sizeof(query),
        "CREATE TABLE IF NOT EXISTS %s(stream INTEGER NOT NULL REFERENCES streams(id) ON DELETE CASCADE,offset INTEGER NOT NULL,length INTEGER NOT NULL CHECK(length BETWEEN 128 AND 262400),record_id INTEGER NOT NULL,parent_id INTEGER NOT NULL,actor_id INTEGER NOT NULL,recipient_id INTEGER NOT NULL,created_at INTEGER NOT NULL,title TEXT NOT NULL,body TEXT NOT NULL,login TEXT NOT NULL,salt BLOB NOT NULL,password_hash BLOB NOT NULL,attributes BLOB NOT NULL,event_type INTEGER NOT NULL,sender_id INTEGER NOT NULL,PRIMARY KEY(stream,offset)) WITHOUT ROWID;"
        "CREATE INDEX IF NOT EXISTS %s_identity ON %s(stream,record_id);"
        "CREATE INDEX IF NOT EXISTS %s_actor ON %s(stream,actor_id,created_at);",row_tables[i],row_tables[i],row_tables[i],row_tables[i],row_tables[i]);
        if(!sql(db,query))return 0;
    }return 1;
}
static int row_kind(Store *s) {
    if(storage_version<2)return 0;
    sqlite3_stmt *q=NULL;int kind=0;
    if(sqlite3_prepare_v2(s->db,"SELECT kind FROM record_streams WHERE stream=?",-1,&q,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_int64(q,1,s->id);int rc=sqlite3_step(q);if(rc==SQLITE_ROW)kind=sqlite3_column_int(q,0);else if(rc!=SQLITE_DONE)kind=-1;
    sqlite3_finalize(q);return kind;
}
static size_t row_size(unsigned kind,const unsigned char *r) {
    if(kind>=4)return kind==15?32:kind==16?512:4096;
    if(kind==1) { unsigned bl=rget(r+12);return bl>262144?0:(rget(r)&0x80000000u?144+bl:1168); }
    if(kind==2) {
        unsigned bl=rget(r+32),count=(r[0]=='u'||r[0]=='x')?r[2]:(r[0]=='t'||r[0]=='v');
        if(bl>262144 || count>16)return 0;
        return r[0] && strchr("swrdctvagux",r[0])?128+bl+4*count:1152;
    }
    if(r[0]=='D' || r[0]=='d') { unsigned bl=rget(r+24);if(bl>262144)return 0;return r[0]=='D'?1152:116+bl<128?128:116+bl; }
    return 128;
}
/* Reconstruction uses one bounded row cache per open stream, not all bodies. */
static int row_load(Store *s,sqlite3_int64 at) {
    if(s->row_cache && at>=s->row_at && at<s->row_at+(sqlite3_int64)s->row_bytes)return 1;
    if(!s->row_cache && !(s->row_cache=malloc(ROW_MAX)))return 0;
    if(s->kind>=4) {
        char query[350];snprintf(query,sizeof(query),"SELECT offset,event_type,sequence,actor_id,target_id,created_at,text,checksum,flags,context_id FROM %s WHERE stream=? AND offset<=? ORDER BY offset DESC LIMIT 1",row_tables[s->kind]);
        sqlite3_stmt *q=NULL;if(sqlite3_prepare_v2(s->db,query,-1,&q,NULL)!=SQLITE_OK)return 0;
        sqlite3_bind_int64(q,1,s->id);sqlite3_bind_int64(q,2,at);
        int ok=sqlite3_step(q)==SQLITE_ROW;
        if(ok) {
            size_t bytes=row_size((unsigned)s->kind,NULL);sqlite3_int64 offset=sqlite3_column_int64(q,0);
            int n=sqlite3_column_bytes(q,6),event=sqlite3_column_int(q,1),flags=sqlite3_column_int(q,8);
            ok=offset>=8 && (offset-8)%(sqlite3_int64)bytes==0 && at<offset+(sqlite3_int64)bytes && n>=0 && n<=(s->kind==15?0:s->kind==16?320:3000) && event>0 && event<=255 && flags>=0 && flags<=(s->kind>=15?1:0);
            if(ok) {
                unsigned char *r=s->row_cache;memset(r,0,bytes);r[0]=(unsigned char)event;r[1]=(unsigned char)flags;
                rput(r+4,(uint32_t)sqlite3_column_int64(q,2));
                rput(r+(s->kind>=15?8:12),(uint32_t)sqlite3_column_int64(q,4));
                rput(r+(s->kind==15?16:s->kind==16?12:8),(uint32_t)sqlite3_column_int64(q,3));
                unsigned stamp_at=s->kind==15?20:s->kind==16?340:16;
                uint64_t stamp=(uint64_t)sqlite3_column_int64(q,5);rput(r+stamp_at,(uint32_t)stamp);rput(r+stamp_at+4,(uint32_t)(stamp>>32));
                if(s->kind>=15)rput(r+(s->kind==15?12:348),(uint32_t)sqlite3_column_int64(q,9));
                if(s->kind!=15) { rput(r+(s->kind==16?16:24),(uint32_t)n);if(n)memcpy(r+(s->kind==16?20:28),sqlite3_column_blob(q,6),(size_t)n); }
                rput(r+bytes-4,(uint32_t)sqlite3_column_int64(q,7));
                ok=event_sum(r,bytes)==rget(r+bytes-4) && (s->kind==15 || !memchr(r+(s->kind==16?20:28),0,(size_t)n));
                if(ok) { s->row_at=offset;s->row_bytes=bytes; }
            }
        }sqlite3_finalize(q);return ok;
    }
    char sqltext[512];snprintf(sqltext,sizeof(sqltext),"SELECT offset,length,record_id,parent_id,actor_id,recipient_id,created_at,title,body,login,salt,password_hash,attributes,event_type,sender_id FROM %s WHERE stream=? AND offset<=? ORDER BY offset DESC LIMIT 1",row_tables[s->kind]);
    sqlite3_stmt *q=NULL;if(sqlite3_prepare_v2(s->db,sqltext,-1,&q,NULL)!=SQLITE_OK)return 0;
    sqlite3_bind_int64(q,1,s->id);sqlite3_bind_int64(q,2,at);int ok=sqlite3_step(q)==SQLITE_ROW;
    if(ok) {
        sqlite3_int64 offset=sqlite3_column_int64(q,0);int n=sqlite3_column_int(q,1),an=sqlite3_column_bytes(q,12),tl=sqlite3_column_bytes(q,7),bl=sqlite3_column_bytes(q,8),nl=sqlite3_column_bytes(q,9);
        ok=n>=128 && n<=ROW_MAX && offset<=at && at<offset+n && tl<=80 && bl<=262144 && nl<=32 && an>=0 && an<=256;
        unsigned char *r=s->row_cache;if(ok) {
            memset(r,0,(size_t)n);const unsigned char *a=sqlite3_column_blob(q,12);
            if(s->kind==1) {
                ok=an==64 && n>=144+bl;
                if(ok) { memcpy(r,a,16);size_t base=(size_t)n-44;memcpy(r+base-4,a+16,48);rput(r,((uint32_t)sqlite3_column_int64(q,2))|(rget(r)&0x80000000u));rput(r+4,(uint32_t)sqlite3_column_int64(q,3));rput(r+8,(unsigned)tl);rput(r+12,(unsigned)bl);if(tl)memcpy(r+16,sqlite3_column_blob(q,7),(size_t)tl);if(bl)memcpy(r+96,sqlite3_column_blob(q,8),(size_t)bl);uint64_t t=(uint64_t)sqlite3_column_int64(q,6);rput(r+base-4,(uint32_t)t);rput(r+base,(uint32_t)(t>>32));if(nl)memcpy(r+base+4,sqlite3_column_blob(q,9),(size_t)nl);rput(r+base+36,(uint32_t)sqlite3_column_int64(q,5)); }
            } else if(s->kind==2) {
                ok=an>=48 && an<=112 && n>=128+bl;
                if(ok) { memcpy(r,a,44);memcpy(r+n-(an-44),a+44,(size_t)(an-44));rput(r+8,(uint32_t)sqlite3_column_int64(q,2));rput(r+12,(uint32_t)sqlite3_column_int64(q,4));rput(r+16,(uint32_t)sqlite3_column_int64(q,14));rput(r+24,(uint32_t)sqlite3_column_int64(q,3));rput(r+20,(uint32_t)sqlite3_column_int64(q,5));rput(r+28,(unsigned)tl);rput(r+32,(unsigned)bl);uint64_t t=(uint64_t)sqlite3_column_int64(q,6);rput(r+36,(uint32_t)t);rput(r+40,(uint32_t)(t>>32));if(tl)memcpy(r+44,sqlite3_column_blob(q,7),(size_t)tl);if(bl)memcpy(r+124,sqlite3_column_blob(q,8),(size_t)bl); }
            } else {
                int draft=sqlite3_column_int(q,13)=='D'||sqlite3_column_int(q,13)=='d';ok=an==(draft?36:128) && (!draft || n>=116+bl);
                if(ok) { if(draft) { memcpy(r,a,32);memcpy(r+n-4,a+32,4);rput(r+20,(unsigned)tl);rput(r+24,(unsigned)bl);if(tl)memcpy(r+32,sqlite3_column_blob(q,7),(size_t)tl);if(bl)memcpy(r+112,sqlite3_column_blob(q,8),(size_t)bl); }else memcpy(r,a,128);
                    rput(r+4,(uint32_t)sqlite3_column_int64(q,2));
                    if(r[0]=='A'||r[0]=='J')if(nl)memcpy(r+8,sqlite3_column_blob(q,9),(size_t)nl);
                    if(strchr("AJPQ",r[0])) { ok=sqlite3_column_bytes(q,10)==16 && sqlite3_column_bytes(q,11)==32;if(ok) { memcpy(r+40,sqlite3_column_blob(q,10),16);memcpy(r+56,sqlite3_column_blob(q,11),32); } }
                }
            }
            if(ok) { s->row_at=offset;s->row_bytes=(size_t)n; }
        }
    }sqlite3_finalize(q);return ok;
}
static int row_block(Store *s,sqlite3_int64 number,unsigned char *out) {
    sqlite3_int64 begin=number*BLOCK,end=begin+BLOCK;if(end>s->length)end=s->length;
    memset(out,0,BLOCK);
    if(begin==0 && end) {
        sqlite3_stmt *q=NULL;int ok=sqlite3_prepare_v2(s->db,"SELECT magic FROM record_streams WHERE stream=?",-1,&q,NULL)==SQLITE_OK;
        if(ok) { sqlite3_bind_int64(q,1,s->id);ok=sqlite3_step(q)==SQLITE_ROW && sqlite3_column_bytes(q,0)==8;if(ok)memcpy(out,sqlite3_column_blob(q,0),8); }sqlite3_finalize(q);if(!ok)return 0;
    }
    sqlite3_int64 at=begin<8?8:begin;
    while(at<end) {
        if(!row_load(s,at))return 0;
        size_t available=(size_t)(s->row_at+(sqlite3_int64)s->row_bytes-at),n=(size_t)(end-at);if(n>available)n=available;
        memcpy(out+(at-begin),s->row_cache+(at-s->row_at),n);at+=(sqlite3_int64)n;
    }return 1;
}
static int row_insert(Store *s,sqlite3_int64 offset,const unsigned char *r,size_t n) {
    if(s->kind>=4) {
        unsigned text_size=s->kind==15?0:rget(r+(s->kind==16?16:24));
        unsigned text_at=s->kind==16?20:28,stamp_at=s->kind==15?20:s->kind==16?340:16;
        if(n!=row_size((unsigned)s->kind,NULL) || !r[0] || r[1]>(s->kind>=15?1:0) || r[2] || r[3] || text_size>(s->kind==16?320:3000) || memchr(r+text_at,0,text_size) || event_sum(r,n)!=rget(r+n-4))return 0;
        if(s->kind!=15)for(size_t i=text_at+text_size;i<n-4;i++)if(!(s->kind==16 && i>=340 && i<352) && r[i])return 0;
        char query[240];snprintf(query,sizeof(query),"INSERT INTO %s VALUES(?,?,?,?,?,?,?,?,?,?,?)",row_tables[s->kind]);
        sqlite3_stmt *q=NULL;if(sqlite3_prepare_v2(s->db,query,-1,&q,NULL)!=SQLITE_OK)return 0;
        sqlite3_bind_int64(q,1,s->id);sqlite3_bind_int64(q,2,offset);sqlite3_bind_int(q,3,r[0]);sqlite3_bind_int64(q,4,rget(r+4));
        sqlite3_bind_int64(q,5,rget(r+(s->kind==15?16:s->kind==16?12:8)));sqlite3_bind_int64(q,6,rget(r+(s->kind>=15?8:12)));
        sqlite3_bind_int64(q,7,(sqlite3_int64)(rget(r+stamp_at)|((uint64_t)rget(r+stamp_at+4)<<32)));
        sqlite3_bind_text(q,8,(const char *)r+text_at,(int)text_size,SQLITE_STATIC);sqlite3_bind_int64(q,9,rget(r+n-4));
        sqlite3_bind_int(q,10,r[1]);sqlite3_bind_int64(q,11,s->kind>=15?rget(r+(s->kind==15?12:348)):0);
        int ok=sqlite3_step(q)==SQLITE_DONE;sqlite3_finalize(q);return ok;
    }
    unsigned char attr[256],salt[16]={0},hash[32]={0};size_t an=0,tl=0,bl=0,nl=0;const unsigned char *title=r,*body=r,*login=r;
    uint32_t id=0,parent=0,actor=0,to=0,sender=0;uint64_t created=0;int auth=0;
    if(s->kind==1) {
        size_t base=n-44;id=rget(r)&0x7fffffffu;parent=rget(r+4);tl=rget(r+8);bl=rget(r+12);title=r+16;body=r+96;login=r+base+4;nl=strnlen((const char *)login,32);created=rget(r+base-4)|((uint64_t)rget(r+base)<<32);to=rget(r+base+36);
        memcpy(attr,r,16);memcpy(attr+16,r+base-4,48);an=64;memset(attr,0,16);rput(attr,rget(r)&0x80000000u);memset(attr+16,0,44);
    } else if(s->kind==2) {
        unsigned count=(r[0]=='u'||r[0]=='x')?r[2]:(r[0]=='t'||r[0]=='v');an=48+4*count;
        id=rget(r+8);parent=rget(r+24);sender=rget(r+16);actor=rget(r+12);to=rget(r+20);tl=rget(r+28);bl=rget(r+32);title=r+44;body=r+124;created=rget(r+36)|((uint64_t)rget(r+40)<<32);
        memcpy(attr,r,44);memcpy(attr+44,r+n-(an-44),an-44);memset(attr+8,0,36);
    } else {
        id=rget(r+4);int draft=r[0]=='D'||r[0]=='d';
        if(draft) { tl=rget(r+20);bl=rget(r+24);title=r+32;body=r+112;memcpy(attr,r,32);memcpy(attr+32,r+n-4,4);an=36;memset(attr+20,0,8); }
        else { memcpy(attr,r,128);an=128; }
        memset(attr+4,0,4);
        if(r[0]=='A'||r[0]=='J') { login=r+8;nl=strnlen((const char *)login,32);memset(attr+8,0,32); }
        auth=strchr("AJPQ",r[0])!=NULL;if(auth) { memcpy(salt,r+40,16);memcpy(hash,r+56,32);memset(attr+40,0,48); }
    }
    if(tl>80 || bl>262144 || nl>32)return 0;
    char query[300];snprintf(query,sizeof(query),"INSERT INTO %s VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",row_tables[s->kind]);
    sqlite3_stmt *q=NULL;int ok=sqlite3_prepare_v2(s->db,query,-1,&q,NULL)==SQLITE_OK;
    if(ok) {
        sqlite3_bind_int64(q,1,s->id);sqlite3_bind_int64(q,2,offset);sqlite3_bind_int64(q,3,(sqlite3_int64)n);sqlite3_bind_int64(q,4,id);sqlite3_bind_int64(q,5,parent);sqlite3_bind_int64(q,6,actor);sqlite3_bind_int64(q,7,to);sqlite3_bind_int64(q,8,(sqlite3_int64)created);
        sqlite3_bind_text(q,9,(const char *)title,(int)tl,SQLITE_STATIC);sqlite3_bind_text(q,10,(const char *)body,(int)bl,SQLITE_STATIC);sqlite3_bind_text(q,11,(const char *)login,(int)nl,SQLITE_STATIC);sqlite3_bind_blob(q,12,salt,auth?16:0,SQLITE_STATIC);sqlite3_bind_blob(q,13,hash,auth?32:0,SQLITE_STATIC);sqlite3_bind_blob(q,14,attr,(int)an,SQLITE_STATIC);sqlite3_bind_int(q,15,s->kind==1?0:r[0]);sqlite3_bind_int64(q,16,sender);ok=sqlite3_step(q)==SQLITE_DONE;
    }sqlite3_finalize(q);return ok;
}
