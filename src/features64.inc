/* Stage64 persistent settings, checkpoint cursors and recoverable group moves.
 * Called only by the board owner; statements always bind user supplied values. */
static sqlite3 *features_db;
static int features_broken;
static int feature_exec(const char *sql) { return sqlite3_exec(features_db,sql,NULL,NULL,NULL)==SQLITE_OK; }
static sqlite3_stmt *feature_stmt(const char *sql) { sqlite3_stmt *s=NULL;if(sqlite3_prepare_v2(features_db,sql,-1,&s,NULL)!=SQLITE_OK)return NULL;return s; }
#include "member_settings_store.inc"
#include "mmm_notes_store.inc"
#include "catalog_group_store.inc"
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
int board_topology_repository_ready(void) { return features_db && !features_broken; }
int board_topology_repository_cascade(uint32_t actor,const BoardDeletion *plan,size_t count) {
    if(!feature_exec("BEGIN IMMEDIATE"))return 0;
    for(size_t i=0;i<count;i++) {
        sqlite3_stmt *s=feature_stmt("INSERT INTO board_deletions VALUES(?,?,?,0) ON CONFLICT(board) DO UPDATE SET actor=excluded.actor,depth=excluded.depth,done=0");
        if(!s) { feature_exec("ROLLBACK");return 0; }
        sqlite3_bind_int(s,1,(int)plan[i].board);sqlite3_bind_int(s,2,(int)actor);
        sqlite3_bind_int(s,3,(int)plan[i].depth);int ok=sqlite3_step(s)==SQLITE_DONE;sqlite3_finalize(s);
        if(!ok) { feature_exec("ROLLBACK");return 0; }
    }
    if(!feature_exec("COMMIT")) { feature_exec("ROLLBACK");return 0; }
    if(!delete_recover() || !access_prune(1)) { features_broken=1;return 0; }
    return 1;
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
    const char *tables[]={"board_deletions","ui_preferences","menu_preferences","guest_boards","news_scoped","news","delegated","group_moves","applications","link_articles","mmm_board_labels","mmm_scanlist","mmm_scan_rules","mmm_signatures","conference_rooms","conference_lines","conference_read"};int ok=1;
    for(unsigned i=0;legacy && ok && i<sizeof(tables)/sizeof(*tables);i++) {
        sqlite3_stmt *q=feature_stmt("SELECT 1 FROM legacy.sqlite_master WHERE type='table' AND name=?");if(!q) { ok=0;break; }sqlite3_bind_text(q,1,tables[i],-1,SQLITE_STATIC);int present=sqlite3_step(q)==SQLITE_ROW;sqlite3_finalize(q);
        if(present) { char command[256];snprintf(command,sizeof(command),"INSERT OR IGNORE INTO %s SELECT * FROM legacy.%s",tables[i],tables[i]);ok=feature_exec(command); }
    }
    if(ok)ok=feature_exec("INSERT INTO storage_imports VALUES('features');COMMIT");else feature_exec("ROLLBACK");
    if(legacy && !feature_exec("DETACH DATABASE legacy"))ok=0;
    return ok;
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
    return feature_exec(storage_database()?"PRAGMA journal_mode=WAL":"PRAGMA journal_mode=DELETE") && feature_exec("PRAGMA synchronous=FULL;CREATE TABLE IF NOT EXISTS board_deletions(board INTEGER PRIMARY KEY,actor INTEGER,depth INTEGER,done INTEGER);CREATE TABLE IF NOT EXISTS menu_preferences(id INTEGER PRIMARY KEY,value INTEGER);CREATE TABLE IF NOT EXISTS guest_boards(id INTEGER PRIMARY KEY,value INTEGER);CREATE TABLE IF NOT EXISTS news_scoped(user INTEGER,kind INTEGER,scope TEXT,stamp INTEGER NOT NULL,PRIMARY KEY(user,kind,scope));CREATE TABLE IF NOT EXISTS news(user INTEGER,kind INTEGER,stamp INTEGER NOT NULL,PRIMARY KEY(user,kind));CREATE TABLE IF NOT EXISTS delegated(board INTEGER,gid INTEGER,PRIMARY KEY(board,gid));CREATE TABLE IF NOT EXISTS group_moves(board INTEGER PRIMARY KEY,actor INTEGER,source INTEGER,destination INTEGER,done INTEGER);CREATE TABLE IF NOT EXISTS applications(user INTEGER,field TEXT,value TEXT,PRIMARY KEY(user,field));") && feature_exec(UI_PREFERENCE_SCHEMA) && feature_exec(MMM_NOTES_SCHEMA) && feature_exec(LINK_ARTICLES_SCHEMA) && conference_schema(features_db) && features_import(base) && catalog_group_repository_recover() && delete_recover();
}
int application_repository_load(uint32_t user,ApplicationNotes *out) {
    if(!out)return 0;
    memset(out,0,sizeof(*out));
    if(!features_db || features_broken || !user || user>MEMBER_MAX)return 0;
    sqlite3_stmt *s=feature_stmt("SELECT field,value FROM applications WHERE user=? ORDER BY field");if(!s)return 0;
    int ok=sqlite3_bind_int(s,1,(int)user)==SQLITE_OK,rc=SQLITE_DONE;
    while(ok && (rc=sqlite3_step(s))==SQLITE_ROW) {
        if(sqlite3_column_type(s,0)!=SQLITE_TEXT || sqlite3_column_type(s,1)!=SQLITE_TEXT) { ok=0;break; }
        const char *key=(const char *)sqlite3_column_text(s,0),*value=(const char *)sqlite3_column_text(s,1);
        int keys=sqlite3_column_bytes(s,0),bytes=sqlite3_column_bytes(s,1),field=-1;
        if(key && keys==7 && !memcmp(key,"FROMBBS",7))field=APPLICATION_FROM_BBS;
        if(key && keys==9 && !memcmp(key,"SYSOPNOTE",9))field=APPLICATION_SYSOP_NOTE;
        if(field<0 || !value || bytes<0 || bytes>320 || strlen(value)!=(size_t)bytes || out->present[field]) { ok=0;break; }
        memcpy(out->values[field],value,(size_t)bytes+1);out->present[field]=1;
    }
    ok=ok && rc==SQLITE_DONE;sqlite3_finalize(s);
    if(!ok)memset(out,0,sizeof(*out));
    return ok;
}
int application_repository_save(uint32_t user,ApplicationNoteField field,const char *value) {
    if(!features_db || features_broken || !user || user>MEMBER_MAX ||
       field<APPLICATION_FROM_BBS || field>APPLICATION_SYSOP_NOTE || !value || strlen(value)>320)return 0;
    const char *key=field==APPLICATION_FROM_BBS?"FROMBBS":"SYSOPNOTE";
    sqlite3_stmt *s=feature_stmt("INSERT INTO applications VALUES(?,?,?) ON CONFLICT(user,field) DO UPDATE SET value=excluded.value");if(!s)return 0;
    int ok=sqlite3_bind_int(s,1,(int)user)==SQLITE_OK && sqlite3_bind_text(s,2,key,-1,SQLITE_STATIC)==SQLITE_OK &&
           sqlite3_bind_text(s,3,value,-1,SQLITE_STATIC)==SQLITE_OK && sqlite3_step(s)==SQLITE_DONE;
    sqlite3_finalize(s);return ok;
}
static unsigned feature_setting(const char *table,uint32_t id) {
    char sql[128];snprintf(sql,sizeof(sql),"SELECT value FROM %s WHERE id=?",table);sqlite3_stmt *s=feature_stmt(sql);if(!s) { features_broken=1;return 0; }
    sqlite3_bind_int(s,1,(int)id);int rc=sqlite3_step(s);unsigned n=rc==SQLITE_ROW?(unsigned)sqlite3_column_int(s,0):(!strcmp(table,"menu_preferences")?1u:0u);if(rc!=SQLITE_ROW && rc!=SQLITE_DONE)features_broken=1;sqlite3_finalize(s);return n;
}
static int feature_setting_save(const char *table,uint32_t id,unsigned value) {
    char sql[160];snprintf(sql,sizeof(sql),"INSERT INTO %s VALUES(?,?) ON CONFLICT(id) DO UPDATE SET value=excluded.value",table);sqlite3_stmt *s=feature_stmt(sql);if(!s)return 0;
    sqlite3_bind_int(s,1,(int)id);sqlite3_bind_int(s,2,(int)value);int ok=sqlite3_step(s)==SQLITE_DONE;sqlite3_finalize(s);return ok;
}
int article_repository_guest_save(uint32_t board,unsigned enabled) {
    return features_db && !features_broken && feature_setting_save("guest_boards",board,enabled);
}
void article_repository_author_changed(uint32_t id,uint32_t user,const char *name) {
    Article *a=article_at(id-1);uint32_t old=user_named(a->author);
    strcpy(a->author,name);
    if(old && user_posts[old])user_posts[old]--;
    user_posts[user]++;
}
static void catalog_group_result(CatalogGroupOutcome result,BoardResult *r) {
    const char *error="ERR command\r\n";
    switch(result) {
    case GROUP_CORE_OK:return;
    case GROUP_CORE_ADMIN_REQUIRED:error="ERR admin required\r\n";break;
    case GROUP_CORE_REVOKED:r->revoked=1;error="ERR session revoked\r\n";break;
    case GROUP_CORE_MEMBER_STORAGE:error="ERR member storage unavailable\r\n";break;
    case GROUP_CORE_MODERATION_STORAGE:error="ERR moderation storage unavailable\r\n";break;
    case GROUP_CORE_POLICY_STORAGE:error="ERR board policy storage\r\n";break;
    case GROUP_CORE_CATALOG_STORAGE:error="ERR catalog storage\r\n";break;
    case GROUP_CORE_SETTINGS_STORAGE:error="ERR settings storage\r\n";break;
    case GROUP_CORE_INVALID:break;
    }
    r->feedback.failed=1;snprintf(r->text,sizeof(r->text),"%s",error);
}
static void catalog_group_execute(const BoardRequest *q,BoardResult *r) {
    CatalogActor actor={q->user,q->user_epoch};CatalogGroupChange change;
    CatalogGroupOutcome result=q->action=='G'?catalog_group_delegate(&actor,q->number,q->cursor):
        catalog_group_move(&actor,q->number,q->cursor,q->audit_actor,&change);
    if(result!=GROUP_CORE_OK) { catalog_group_result(result,r);return; }
    if(q->action=='G') { strcpy(r->text,"SIGOP GROUP SAVED\r\n");return; }
    for(uint32_t u=1;u<=change.through;u++)vault_worker_account(u,moderation_epoch(u));
    r->all_accounts_changed=1;r->board_refresh=1;r->password_changed=1;
    r->account_epoch=change.actor_epoch;strcpy(r->text,"BOARD GROUP MOVED\r\n");
}
static void article_management_result(ArticleOutcome result,BoardResult *r) {
    switch(result) {
    case ARTICLE_REVOKED:r->revoked=1;(r->feedback.failed=1,strcpy(r->text,"ERR session revoked\r\n"));break;
    case ARTICLE_LOGIN_REQUIRED:(r->feedback.failed=1,strcpy(r->text,"ERR login required\r\n"));break;
    case ARTICLE_ADMIN_REQUIRED:(r->feedback.failed=1,strcpy(r->text,"ERR admin required\r\n"));break;
    case ARTICLE_MANAGEMENT_INVALID:(r->feedback.failed=1,strcpy(r->text,"ERR command\r\n"));break;
    case ARTICLE_MEMBER_STORAGE:(r->feedback.failed=1,strcpy(r->text,"ERR member storage unavailable\r\n"));break;
    case ARTICLE_MODERATION_STORAGE:(r->feedback.failed=1,strcpy(r->text,"ERR moderation storage unavailable\r\n"));break;
    case ARTICLE_POLICY_STORAGE:(r->feedback.failed=1,strcpy(r->text,"ERR board policy storage\r\n"));break;
    case ARTICLE_EXTRAS_STORAGE:(r->feedback.failed=1,strcpy(r->text,"ERR extras storage\r\n"));break;
    case ARTICLE_SETTINGS_STORAGE:(r->feedback.failed=1,strcpy(r->text,"ERR settings storage\r\n"));break;
    default:(r->feedback.failed=1,strcpy(r->text,"ERR article storage\r\n"));break;
    }
}
static void advanced_execute(const BoardRequest *q,BoardResult *r) {
    if(!q->user) { (r->feedback.failed=1,strcpy(r->text,"ERR login required\r\n"));return; }
    if(q->action=='J') { r->service_action='J';r->service_number=q->number;r->service_next=(unsigned)members_level(q->user);return; }
    if(q->action=='v') {
        MemberActor actor={q->user,q->user_epoch};
        MemberOutcome result=member_core_menu_save(&actor,(MemberMenuMode)q->number);
        if(result==MEMBER_CORE_INVALID)goto invalid;
        if(result!=MEMBER_CORE_OK) { member_result(result,r);return; }
        strcpy(r->text,"MENU MODE SAVED\r\n");return;
    }
    if(q->action=='p' || q->action=='o') {
        ArticleActor actor={q->user,q->user_epoch,q->board_grants};
        ArticleOutcome result=q->action=='p'?article_guest_set(&actor,q->number,q->cursor):
            article_author_set(&actor,q->number,q->cursor);
        if(result!=ARTICLE_OK) { article_management_result(result,r);return; }
        if(q->action=='p') { r->board_refresh=1;strcpy(r->text,"BOARD GUEST SAVED\r\n"); }
        else snprintf(r->text,sizeof(r->text),"AUTHOR CHANGED %u %u\r\n",q->number,q->cursor);
        return;
    }
    if(q->action=='N' || q->action=='n') {
        MemberActor actor={q->user,q->user_epoch};MemberCheckWindow window;
        MemberOutcome result=q->action=='N'?member_core_check_begin(&actor,(MemberCheckKind)q->number,q->title,&window):
            member_core_check_commit(&actor,(MemberCheckKind)q->number,q->title,q->until);
        if(result==MEMBER_CORE_INVALID)goto invalid;
        if(result!=MEMBER_CORE_OK) { member_result(result,r);return; }
        if(q->action=='N') { r->service_action='n';r->service_number=q->number;r->since=window.since;r->until=window.until; }
        else { r->feedback.checkpoint_saved=1;strcpy(r->text,"CHECKPOINT SAVED\r\n"); }
        return;
    }
    if(q->action=='M') {
        if(!board_feature_enabled("mail"))goto invalid;
        MailActor actor={q->user,q->user_epoch};MailOutcome result=mail_core_import(&actor,q->number);
        if(result!=MAIL_CORE_OK) { mail_result(q,result,r);return; }
        strcpy(r->text,"MAIL IMPORTED TO BUFFER\r\n");return;
    }
    if(q->action=='B') {
        MailActor actor={q->user,q->user_epoch};MailAttachments attachments;
        MailOutcome result=mail_core_attachment_batch(&actor,q->number,&attachments);
        if(result==MAIL_CORE_DISABLED) { strcpy(r->text,"MAIL ERROR unavailable\r\n");return; }
        if(result!=MAIL_CORE_OK) { mail_result(q,result,r);return; }
        r->feedback.mail_batch=1;r->feedback.count=attachments.count;memcpy(r->feedback.ids,attachments.ids,attachments.count*sizeof(*attachments.ids));
        size_t n=(size_t)snprintf(r->text,sizeof(r->text),"MAILBATCH");
        for(unsigned i=0;i<attachments.count;i++)n+=(size_t)snprintf(r->text+n,sizeof(r->text)-n," %u",attachments.ids[i]);
        snprintf(r->text+n,sizeof(r->text)-n,"\r\n");return;
    }
    if(q->action=='D') { board_topology_execute(q,q->number,1,r);return; }
    if(q->action=='G' || q->action=='g') { catalog_group_execute(q,r);return; }
    if(!moderation_admin(q->user)) { (r->feedback.failed=1,strcpy(r->text,"ERR admin required\r\n"));return; }
    if(q->action=='A') {
        MemberActor actor={q->user,q->user_epoch};MemberApplication application;
        MemberOutcome result=member_core_application_get(&actor,q->number,&application);
        if(result!=MEMBER_CORE_OK) { member_result(result,r);return; }
        OPERATION_PAGE(r,APPLICATION,application,application);
        size_t n=(size_t)snprintf(r->text,sizeof(r->text),"APPLICATION %u %s\r\n",application.profile.user,application.profile.name);
        for(unsigned f=0;f<PROFILE_FIELDS;f++)n+=(size_t)snprintf(r->text+n,sizeof(r->text)-n,"%s %s\r\n",profile_key(f),member_profile_value(&application.profile,f));
        const char *keys[]={"FROMBBS","SYSOPNOTE"};
        for(unsigned f=0;f<2;f++)if(application.notes.present[f])
            n+=(size_t)snprintf(r->text+n,sizeof(r->text)-n,"%s %s\r\n",keys[f],application.notes.values[f]);
        snprintf(r->text+n,sizeof(r->text)-n,"END APPLICATION\r\n");return;
    }
    if(q->action=='a') {
        MemberActor actor={q->user,q->user_epoch};
        ApplicationNoteField field=!strcmp(q->title,"FROMBBS")?APPLICATION_FROM_BBS:
                                   !strcmp(q->title,"SYSOPNOTE")?APPLICATION_SYSOP_NOTE:(ApplicationNoteField)-1;
        MemberOutcome result=member_core_application_set(&actor,q->number,field,q->body);
        if(result!=MEMBER_CORE_OK)member_result(result,r);else strcpy(r->text,"APPLICATION SAVED\r\n");
        return;
    }
invalid:(r->feedback.failed=1,strcpy(r->text,"ERR command\r\n"));return;
}
