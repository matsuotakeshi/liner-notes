/* Stage64 persistent settings, checkpoint cursors and recoverable group moves.
 * Called only by the board owner; statements always bind user supplied values. */
static sqlite3 *features_db;
static int features_broken;
static int feature_exec(const char *sql) { return sqlite3_exec(features_db,sql,NULL,NULL,NULL)==SQLITE_OK; }
static sqlite3_stmt *feature_stmt(const char *sql) { sqlite3_stmt *s=NULL;if(sqlite3_prepare_v2(features_db,sql,-1,&s,NULL)!=SQLITE_OK)return NULL;return s; }
static uint64_t checked_time(uint32_t user,unsigned kind,const char *scope) {
    sqlite3_stmt *s=feature_stmt(*scope?"SELECT stamp FROM news_scoped WHERE user=? AND kind=? AND scope=?":"SELECT stamp FROM news WHERE user=? AND kind=?");if(!s)return 0;
    sqlite3_bind_int(s,1,(int)user);sqlite3_bind_int(s,2,(int)kind);if(*scope)sqlite3_bind_text(s,3,scope,-1,SQLITE_TRANSIENT);uint64_t value=sqlite3_step(s)==SQLITE_ROW?(uint64_t)sqlite3_column_int64(s,0):0;sqlite3_finalize(s);return value;
}
static int save_checked(uint32_t user,unsigned kind,uint64_t stamp,const char *scope) {
    sqlite3_stmt *s=feature_stmt(*scope?"INSERT INTO news_scoped(user,kind,stamp,scope) VALUES(?,?,?,?) ON CONFLICT(user,kind,scope) DO UPDATE SET stamp=max(stamp,excluded.stamp)":"INSERT INTO news VALUES(?,?,?) ON CONFLICT(user,kind) DO UPDATE SET stamp=max(stamp,excluded.stamp)");if(!s)return 0;
    sqlite3_bind_int(s,1,(int)user);sqlite3_bind_int(s,2,(int)kind);sqlite3_bind_int64(s,3,(sqlite3_int64)stamp);if(*scope)sqlite3_bind_text(s,4,scope,-1,SQLITE_TRANSIENT);int ok=sqlite3_step(s)==SQLITE_DONE;sqlite3_finalize(s);return ok;
}
static int group_delegated(uint32_t board,uint32_t user,unsigned group) {
    return catalog_sigop(board,user) && catalog_group(board,group);
}
static int feature_groups_recover(void) {
    for(;;) {
        sqlite3_stmt *s=feature_stmt("SELECT board,actor,source,destination FROM group_moves WHERE done=0 LIMIT 1");if(!s)return 0;
        int rc=sqlite3_step(s);if(rc==SQLITE_DONE) { sqlite3_finalize(s);return 1; }if(rc!=SQLITE_ROW) { sqlite3_finalize(s);return 0; }
        uint32_t board=(uint32_t)sqlite3_column_int(s,0),actor=(uint32_t)sqlite3_column_int(s,1);unsigned from=(unsigned)sqlite3_column_int(s,2),to=(unsigned)sqlite3_column_int(s,3);sqlite3_finalize(s);
        if(catalog_replace_group(board,actor,from,to)<=0 || members_group_move(actor,from,to)<=0)return 0;
        if(!feature_exec("BEGIN IMMEDIATE"))return 0;
        s=feature_stmt("INSERT OR IGNORE INTO delegated SELECT board,? FROM delegated WHERE board=? AND gid=?");if(!s)goto rollback;
        sqlite3_bind_int(s,1,(int)to);sqlite3_bind_int(s,2,(int)board);sqlite3_bind_int(s,3,(int)from);rc=sqlite3_step(s);sqlite3_finalize(s);if(rc!=SQLITE_DONE)goto rollback;
        s=feature_stmt("DELETE FROM delegated WHERE board=? AND gid=?");if(!s)goto rollback;
        sqlite3_bind_int(s,1,(int)board);sqlite3_bind_int(s,2,(int)from);rc=sqlite3_step(s);sqlite3_finalize(s);if(rc!=SQLITE_DONE)goto rollback;
        s=feature_stmt("UPDATE group_moves SET done=1 WHERE board=?");if(!s)goto rollback;
        sqlite3_bind_int(s,1,(int)board);rc=sqlite3_step(s);sqlite3_finalize(s);if(rc!=SQLITE_DONE || !feature_exec("COMMIT"))goto rollback;
        continue;
rollback:feature_exec("ROLLBACK");return 0;
    }
}
static int delete_recover(void) {
    for(;;) {
        sqlite3_stmt *s=feature_stmt("SELECT board,actor FROM board_deletions WHERE done=0 ORDER BY depth DESC,board LIMIT 1");if(!s)return 0;
        int rc=sqlite3_step(s);if(rc==SQLITE_DONE) { sqlite3_finalize(s);return 1; }if(rc!=SQLITE_ROW) { sqlite3_finalize(s);return 0; }
        uint32_t b=(uint32_t)sqlite3_column_int(s,0),actor=(uint32_t)sqlite3_column_int(s,1);sqlite3_finalize(s);
        if(b==1 || !catalog_known(b))return 0;
        for(uint32_t i=1;i<=article_count;i++)if(on_board(i,b) && !extras_removed(i) && !extras_set('D',i,actor,""))return 0;
        if(!policy_deleted(b) && !policy_delete(b,actor))return 0;
        s=feature_stmt("UPDATE board_deletions SET done=1 WHERE board=?");if(!s)return 0;sqlite3_bind_int(s,1,(int)b);rc=sqlite3_step(s);sqlite3_finalize(s);if(rc!=SQLITE_DONE)return 0;
    }
}
static int features_import(const char *base) {
    if(!storage_database())return 1;
    if(!feature_exec("CREATE TABLE IF NOT EXISTS storage_imports(name TEXT PRIMARY KEY)"))return 0;
    sqlite3_stmt *done=feature_stmt("SELECT 1 FROM storage_imports WHERE name='features'");if(!done)return 0;int exists=sqlite3_step(done)==SQLITE_ROW;sqlite3_finalize(done);if(exists)return 1;
    char path[4096];if(snprintf(path,sizeof(path),"%s.features.sqlite",base)>=(int)sizeof(path))return 0;
    struct stat st;int legacy=!lstat(path,&st);if(legacy && (!S_ISREG(st.st_mode) || st.st_nlink!=1))return 0;
    if(legacy) {
        sqlite3_stmt *attach=feature_stmt("ATTACH DATABASE ? AS legacy");if(!attach)return 0;sqlite3_bind_text(attach,1,path,-1,SQLITE_STATIC);int ok=sqlite3_step(attach)==SQLITE_DONE;sqlite3_finalize(attach);if(!ok)return 0;
    }
    if(!feature_exec("BEGIN IMMEDIATE"))return 0;
    const char *tables[]={"board_deletions","menu_preferences","guest_boards","news_scoped","news","delegated","group_moves","applications"};int ok=1;
    for(unsigned i=0;legacy && ok && i<sizeof(tables)/sizeof(*tables);i++) {
        sqlite3_stmt *q=feature_stmt("SELECT 1 FROM legacy.sqlite_master WHERE type='table' AND name=?");if(!q) { ok=0;break; }sqlite3_bind_text(q,1,tables[i],-1,SQLITE_STATIC);int present=sqlite3_step(q)==SQLITE_ROW;sqlite3_finalize(q);
        if(present) { char command[256];snprintf(command,sizeof(command),"INSERT OR IGNORE INTO %s SELECT * FROM legacy.%s",tables[i],tables[i]);ok=feature_exec(command); }
    }
    if(ok)ok=feature_exec("INSERT INTO storage_imports VALUES('features');COMMIT");else feature_exec("ROLLBACK");
    if(legacy && !feature_exec("DETACH DATABASE legacy"))ok=0;return ok;
}
static int features_open(const char *base) {
    char path[4096];if(snprintf(path,sizeof(path),"%s.features.sqlite",base)>=(int)sizeof(path))return 0;
#ifndef _WIN32
    struct stat st;if(!lstat(path,&st) && !S_ISREG(st.st_mode))return 0;
#endif
    if(sqlite3_open_v2(storage_database()?storage_database():path,&features_db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,NULL)!=SQLITE_OK)return 0;
#ifndef _WIN32
    if(!storage_database() && chmod(path,0600))return 0;
#endif
    sqlite3_busy_timeout(features_db,5000);
    return feature_exec(storage_database()?"PRAGMA journal_mode=WAL":"PRAGMA journal_mode=DELETE") && feature_exec("PRAGMA synchronous=FULL;CREATE TABLE IF NOT EXISTS board_deletions(board INTEGER PRIMARY KEY,actor INTEGER,depth INTEGER,done INTEGER);CREATE TABLE IF NOT EXISTS menu_preferences(id INTEGER PRIMARY KEY,value INTEGER);CREATE TABLE IF NOT EXISTS guest_boards(id INTEGER PRIMARY KEY,value INTEGER);CREATE TABLE IF NOT EXISTS news_scoped(user INTEGER,kind INTEGER,scope TEXT,stamp INTEGER NOT NULL,PRIMARY KEY(user,kind,scope));CREATE TABLE IF NOT EXISTS news(user INTEGER,kind INTEGER,stamp INTEGER NOT NULL,PRIMARY KEY(user,kind));CREATE TABLE IF NOT EXISTS delegated(board INTEGER,gid INTEGER,PRIMARY KEY(board,gid));CREATE TABLE IF NOT EXISTS group_moves(board INTEGER PRIMARY KEY,actor INTEGER,source INTEGER,destination INTEGER,done INTEGER);CREATE TABLE IF NOT EXISTS applications(user INTEGER,field TEXT,value TEXT,PRIMARY KEY(user,field));") && features_import(base) && feature_groups_recover() && delete_recover();
}
static int application_value(uint32_t user,const char *field,const char *value) {
    sqlite3_stmt *s=feature_stmt("INSERT INTO applications VALUES(?,?,?) ON CONFLICT(user,field) DO UPDATE SET value=excluded.value");if(!s)return 0;
    sqlite3_bind_int(s,1,(int)user);sqlite3_bind_text(s,2,field,-1,SQLITE_STATIC);sqlite3_bind_text(s,3,value,-1,SQLITE_STATIC);int ok=sqlite3_step(s)==SQLITE_DONE;sqlite3_finalize(s);return ok;
}
static int registration_details(uint32_t user,const char *body) {
    char copy[8193];if(strlen(body)>=sizeof(copy))return 0;strcpy(copy,body);char *line=copy;
    while(*line) { char *end=strchr(line,'\n'),*bar=strchr(line,'|');if(!end || !bar || bar>end)return 0;*end=0;*bar++=0;int field=profile_field(line);
        if(field<0 && strcmp(line,"FROMBBS") && strcmp(line,"SYSOPNOTE"))return 0;
        if(strlen(bar)>320 || (field>=0 && !profile_text_valid((unsigned)field,bar)))return 0;
        if(user && (field>=0?profile_set(user,(unsigned)field,bar)!=1:!application_value(user,line,bar)))return 0;
        line=end+1;
    }return 1;
}
static unsigned feature_setting(const char *table,uint32_t id) {
    char sql[128];snprintf(sql,sizeof(sql),"SELECT value FROM %s WHERE id=?",table);sqlite3_stmt *s=feature_stmt(sql);if(!s) { features_broken=1;return 0; }
    sqlite3_bind_int(s,1,(int)id);int rc=sqlite3_step(s);unsigned n=rc==SQLITE_ROW?(unsigned)sqlite3_column_int(s,0):(!strcmp(table,"menu_preferences")?1u:0u);if(rc!=SQLITE_ROW && rc!=SQLITE_DONE)features_broken=1;sqlite3_finalize(s);return n;
}
static int feature_setting_save(const char *table,uint32_t id,unsigned value) {
    char sql[160];snprintf(sql,sizeof(sql),"INSERT INTO %s VALUES(?,?) ON CONFLICT(id) DO UPDATE SET value=excluded.value",table);sqlite3_stmt *s=feature_stmt(sql);if(!s)return 0;
    sqlite3_bind_int(s,1,(int)id);sqlite3_bind_int(s,2,(int)value);int ok=sqlite3_step(s)==SQLITE_DONE;sqlite3_finalize(s);return ok;
}
static void advanced_execute(const BoardRequest *q,BoardResult *r) {
    if(!q->user) { strcpy(r->text,"ERR login required\r\n");return; }
    if(q->action=='J') { r->service_action='J';r->service_number=q->number;r->service_next=(unsigned)members_level(q->user);return; }
    if(q->action=='v') { if(q->number>2)goto invalid;strcpy(r->text,feature_setting_save("menu_preferences",q->user,q->number)?"MENU MODE SAVED\r\n":"ERR settings storage\r\n");return; }
    if(q->action=='p') {
        if(!catalog_manage(q->number,q->user)) { strcpy(r->text,"ERR admin required\r\n");return; }
        if(q->cursor>1)goto invalid;
        if(!feature_setting_save("guest_boards",q->number,q->cursor))goto storage;
        catalog_guest(q->number,(int)q->cursor);r->board_refresh=1;strcpy(r->text,"BOARD GUEST SAVED\r\n");return;
    }
    if(q->action=='o') {
        if(!q->number || q->number>article_count || unavailable(q->number) || !catalog_manage(board_of(q->number),q->user)) { strcpy(r->text,"ERR admin required\r\n");return; }
        if(!members_active(q->cursor) || members_pending(q->cursor) || moderation_suspended(q->cursor))goto invalid;
        Article *a=article_at(q->number-1);uint32_t old=user_named(a->author);const char *name=members_name(q->cursor);
        if(!extras_set('A',q->number,q->user,name))goto storage;
        strcpy(a->author,name);if(old && user_posts[old])user_posts[old]--;user_posts[q->cursor]++;
        snprintf(r->text,sizeof(r->text),"AUTHOR CHANGED %u %u\r\n",q->number,q->cursor);return;
    }
    if(q->action=='N') { if(q->number>2)goto invalid;r->service_action='n';r->service_number=q->number;r->since=checked_time(q->user,q->number,q->title);r->until=(uint64_t)time(NULL);return; }
    if(q->action=='n') { if(q->number>2 || q->until>(uint64_t)time(NULL))goto invalid;strcpy(r->text,save_checked(q->user,q->number,q->until,q->title)?"CHECKPOINT SAVED\r\n":"ERR settings storage\r\n");return; }
    if(q->action=='M') {
        if(!board_feature_enabled("mail"))goto invalid;
        const MailMessage *m=mailbox_get(q->number,q->user);if(!m) { strcpy(r->text,"MAIL ERROR unavailable\r\n");return; }
        const char *body=mailbox_body(m);
        if(mailbox_broken() || profile_set(q->user,PROFILE_BUFFER,body)!=1) { strcpy(r->text,"ERR storage unavailable\r\n");return; }
        strcpy(r->text,"MAIL IMPORTED TO BUFFER\r\n");return;
    }
    if(q->action=='B') {
        const MailMessage *m=mailbox_get(q->number,q->user);if(!board_feature_enabled("mail") || !m || !m->attachments.count) { strcpy(r->text,"MAIL ERROR unavailable\r\n");return; }
        size_t n=(size_t)snprintf(r->text,sizeof(r->text),"MAILBATCH");for(unsigned i=0;i<m->attachments.count;i++)n+=(size_t)snprintf(r->text+n,sizeof(r->text)-n," %u",m->attachments.ids[i]);snprintf(r->text+n,sizeof(r->text)-n,"\r\n");return;
    }
    if(!moderation_admin(q->user)) { strcpy(r->text,"ERR admin required\r\n");return; }
    if(q->action=='D') {
        if(q->number==1 || !catalog_exists(q->number) || !require_v5())goto invalid;
        if(!feature_exec("BEGIN IMMEDIATE"))goto storage;
        for(uint32_t b=2;b<=CATALOG_MAX;b++)if(catalog_under(b,q->number)) {
            sqlite3_stmt *s=feature_stmt("INSERT INTO board_deletions VALUES(?,?,?,0) ON CONFLICT(board) DO UPDATE SET actor=excluded.actor,depth=excluded.depth,done=0");
            if(!s) { feature_exec("ROLLBACK");goto storage; }
            sqlite3_bind_int(s,1,(int)b);sqlite3_bind_int(s,2,(int)q->user);sqlite3_bind_int(s,3,(int)catalog_depth(b));int ok=sqlite3_step(s)==SQLITE_DONE;sqlite3_finalize(s);if(!ok) { feature_exec("ROLLBACK");goto storage; }
        }
        if(!feature_exec("COMMIT")) { feature_exec("ROLLBACK");goto storage; }
        if(!delete_recover() || !access_prune(1)) { features_broken=1;goto storage; }
        r->board_refresh=1;r->board=1;strcpy(r->text,"BOARD TREE DELETED\r\n");return;
    }
    if(q->action=='G') {
        if(!catalog_exists(q->number) || q->cursor>100 || (q->cursor && !catalog_group(q->number,q->cursor)))goto invalid;
        sqlite3_stmt *s=feature_stmt(q->cursor?"INSERT OR IGNORE INTO delegated VALUES(?,?)":"DELETE FROM delegated WHERE board=?");if(!s)goto storage;
        sqlite3_bind_int(s,1,(int)q->number);if(q->cursor)sqlite3_bind_int(s,2,(int)q->cursor);int ok=sqlite3_step(s)==SQLITE_DONE;sqlite3_finalize(s);if(!ok)goto storage;strcpy(r->text,"SIGOP GROUP SAVED\r\n");return;
    }
    if(q->action=='g') {
        if(!catalog_exists(q->number) || !q->cursor || q->cursor>100 || !q->audit_actor || q->audit_actor>100 || q->cursor==q->audit_actor || !catalog_group(q->number,q->cursor))goto invalid;
        sqlite3_stmt *s=feature_stmt("INSERT INTO group_moves VALUES(?,?,?,?,0) ON CONFLICT(board) DO UPDATE SET actor=excluded.actor,source=excluded.source,destination=excluded.destination,done=0");if(!s)goto storage;
        sqlite3_bind_int(s,1,(int)q->number);sqlite3_bind_int(s,2,(int)q->user);sqlite3_bind_int(s,3,(int)q->cursor);sqlite3_bind_int(s,4,(int)q->audit_actor);int ok=sqlite3_step(s)==SQLITE_DONE;sqlite3_finalize(s);if(!ok)goto storage;if(!feature_groups_recover()) { features_broken=1;goto storage; }
        for(uint32_t u=1;u<=members_count();u++) { moderation_credentials_changed(u);vault_worker_account(u,moderation_epoch(u)); }
        r->all_accounts_changed=1;r->board_refresh=1;r->password_changed=1;r->account_epoch=moderation_epoch(q->user);strcpy(r->text,"BOARD GROUP MOVED\r\n");return;
    }
    if(q->action=='A') {
        if(!members_pending(q->number))goto invalid;
        size_t n=(size_t)snprintf(r->text,sizeof(r->text),"APPLICATION %u %s\r\n",q->number,members_name(q->number));
        for(unsigned f=0;f<PROFILE_FIELDS;f++)n+=(size_t)snprintf(r->text+n,sizeof(r->text)-n,"%s %s\r\n",profile_key(f),profile_value(q->number,f));
        sqlite3_stmt *s=feature_stmt("SELECT field,value FROM applications WHERE user=? ORDER BY field");if(!s)goto storage;sqlite3_bind_int(s,1,(int)q->number);while(sqlite3_step(s)==SQLITE_ROW)n+=(size_t)snprintf(r->text+n,sizeof(r->text)-n,"%s %s\r\n",sqlite3_column_text(s,0),sqlite3_column_text(s,1));sqlite3_finalize(s);snprintf(r->text+n,sizeof(r->text)-n,"END APPLICATION\r\n");return;
    }
    if(q->action=='a') {
        if(!members_pending(q->number) || (strcmp(q->title,"FROMBBS") && strcmp(q->title,"SYSOPNOTE")) || strlen(q->body)>320)goto invalid;
        strcpy(r->text,application_value(q->number,q->title,q->body)?"APPLICATION SAVED\r\n":"ERR settings storage\r\n");return;
    }
invalid:strcpy(r->text,"ERR command\r\n");return;
storage:strcpy(r->text,"ERR settings storage\r\n");
}
