/* Vault and mail-attachment vault share a format, but keep separate stream IDs. */
static int vault_row_schema(sqlite3 *db) {
    return sql(db,"CREATE TABLE IF NOT EXISTS vault_records("
        "stream INTEGER NOT NULL REFERENCES streams(id) ON DELETE CASCADE,offset INTEGER NOT NULL CHECK(offset>=8 AND (offset-8)%640=0),"
        "event_type INTEGER NOT NULL CHECK(event_type BETWEEN 1 AND 255),file_id INTEGER NOT NULL CHECK(file_id BETWEEN 0 AND 2147483647),"
        "size INTEGER NOT NULL CHECK(size BETWEEN 0 AND 4294967295),created_at INTEGER NOT NULL CHECK(created_at>=0),"
        "sha256 BLOB NOT NULL CHECK(length(sha256) IN (0,32)),name TEXT NOT NULL CHECK(length(CAST(name AS BLOB))<=100),"
        "description TEXT NOT NULL CHECK(length(CAST(description AS BLOB))<=240),publisher TEXT NOT NULL CHECK(length(CAST(publisher AS BLOB))<=79),"
        "category TEXT NOT NULL CHECK(length(CAST(category AS BLOB))<=48),actor TEXT NOT NULL CHECK(length(CAST(actor AS BLOB))<=79),"
        "text TEXT NOT NULL CHECK(length(CAST(text AS BLOB))<=240),checksum BLOB NOT NULL CHECK(length(checksum)=32),"
        "PRIMARY KEY(stream,offset)) WITHOUT ROWID;"
        "CREATE INDEX IF NOT EXISTS vault_record_file ON vault_records(stream,file_id,offset);"
        "CREATE INDEX IF NOT EXISTS vault_record_date ON vault_records(stream,created_at,offset);");
}
static int vault_entry_event(int event) { return event && strchr("ADRar",event)!=NULL; }
static int vault_row_encode(sqlite3_stmt *q,unsigned char r[640]) {
    memset(r,0,640);
    for(int i=0;i<4;i++)if(sqlite3_column_type(q,i)!=SQLITE_INTEGER || sqlite3_column_int64(q,i)<0)return 0;
    sqlite3_int64 event=sqlite3_column_int64(q,0),id=sqlite3_column_int64(q,1),size=sqlite3_column_int64(q,2),stamp=sqlite3_column_int64(q,3);
    if(event<1 || event>255 || id>2147483647 || size>UINT32_MAX)return 0;
    r[0]=(unsigned char)event;rput(r+4,(uint32_t)id);int entry=vault_entry_event((int)event);
    if(!entry && !strchr("BnoijkpvCHPXESFOTNQ",(int)event))return 0;
    unsigned at=entry?12:8;rput(r+at,(uint32_t)stamp);rput(r+at+4,(uint32_t)((uint64_t)stamp>>32));
    if(sqlite3_column_type(q,4)!=SQLITE_BLOB || sqlite3_column_bytes(q,4)!=(entry?32:0) || sqlite3_column_type(q,11)!=SQLITE_BLOB || sqlite3_column_bytes(q,11)!=32)return 0;
    if(entry) { rput(r+8,(uint32_t)size);memcpy(r+20,sqlite3_column_blob(q,4),32); }
    else if(size)return 0;
    const unsigned positions[]={60,188,444,532,24,104},lengths[]={52,56,524,528,16,20},maxima[]={100,240,79,48,79,240};
    for(int i=0;i<6;i++) {
        int bytes=sqlite3_column_bytes(q,i+5);const unsigned char *value=sqlite3_column_blob(q,i+5);
        if(sqlite3_column_type(q,i+5)!=SQLITE_TEXT || bytes<0 || (unsigned)bytes>maxima[i] || (bytes && memchr(value,0,(size_t)bytes)))return 0;
        int used=entry?i<4:i>=4;if(i==3 && event!='a' && event!='r')used=0;
        if(!used) { if(bytes)return 0;continue; }
        rput(r+lengths[i],(uint32_t)bytes);if(bytes)memcpy(r+positions[i],value,(size_t)bytes);
    }
    memcpy(r+608,sqlite3_column_blob(q,11),32);
    unsigned char sum[32];unsigned bytes=0;
    return EVP_Digest(r,608,sum,&bytes,EVP_sha256(),NULL)==1 && bytes==32 && !memcmp(r+608,sum,32);
}
static int vault_row_load(Store *s,sqlite3_int64 at) {
    sqlite3_stmt *q=NULL;int ok=sqlite3_prepare_v2(s->db,"SELECT event_type,file_id,size,created_at,sha256,name,description,publisher,category,actor,text,checksum,offset FROM vault_records WHERE stream=? AND offset<=? ORDER BY offset DESC LIMIT 1",-1,&q,NULL)==SQLITE_OK;
    if(ok) {
        sqlite3_bind_int64(q,1,s->id);sqlite3_bind_int64(q,2,at);ok=sqlite3_step(q)==SQLITE_ROW;
        if(ok) { sqlite3_int64 offset=sqlite3_column_int64(q,12);ok=offset>=8 && (offset-8)%640==0 && at-offset<640 && vault_row_encode(q,s->row_cache);if(ok) { s->row_at=offset;s->row_bytes=640; } }
    }
    sqlite3_finalize(q);return ok;
}
static int vault_row_insert(Store *s,sqlite3_int64 offset,const unsigned char *r,size_t bytes) {
    if(bytes!=640)return 0;
    int entry=vault_entry_event(r[0]);unsigned stamp_at=entry?12:8;
    uint64_t stamp=rget(r+stamp_at)|((uint64_t)rget(r+stamp_at+4)<<32);if(stamp>INT64_MAX)return 0;
    sqlite3_stmt *q=NULL;int ok=sqlite3_prepare_v2(s->db,"INSERT INTO vault_records VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)",-1,&q,NULL)==SQLITE_OK;
    if(ok) {
        sqlite3_bind_int64(q,1,s->id);sqlite3_bind_int64(q,2,offset);sqlite3_bind_int(q,3,r[0]);sqlite3_bind_int64(q,4,rget(r+4));
        sqlite3_bind_int64(q,5,entry?rget(r+8):0);sqlite3_bind_int64(q,6,(sqlite3_int64)stamp);
        sqlite3_bind_blob(q,7,r+20,entry?32:0,SQLITE_STATIC);
        const unsigned positions[]={60,188,444,532,24,104},lengths[]={52,56,524,528,16,20},maxima[]={100,240,79,48,79,240};
        for(unsigned i=0;ok && i<6;i++) {
            int used=entry?i<4:i>=4;if(i==3 && r[0]!='a' && r[0]!='r')used=0;
            unsigned n=used?rget(r+lengths[i]):0;
            if(n>maxima[i]) { ok=0;break; }
            sqlite3_bind_text(q,(int)i+8,(const char *)r+positions[i],(int)n,SQLITE_STATIC);
        }
        sqlite3_bind_blob(q,14,r+608,32,SQLITE_STATIC);if(ok)ok=sqlite3_step(q)==SQLITE_DONE;
    }
    sqlite3_finalize(q);
    /* Reject unrepresented bytes, corrupt checksums and noncanonical layouts. */
    if(ok) {
        unsigned char canonical[640];Store check=*s;check.row_cache=canonical;
        ok=vault_row_load(&check,offset) && !memcmp(canonical,r,640);
    }
    return ok;
}
