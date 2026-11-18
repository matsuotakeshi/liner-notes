/* Provenance, not the displayed author or current board, decides who may
 * originate a withdrawal. Include unfinished intents and fail closed. */
static int link_imported(uint32_t id) {
    sqlite3_stmt *s=feature_stmt("SELECT 1 FROM link_articles WHERE local_id=?");
    if(!s) { features_broken=1;return -1; }
    sqlite3_bind_int64(s,1,id);int rc=sqlite3_step(s);sqlite3_finalize(s);
    if(rc==SQLITE_ROW)return 1;
    if(rc==SQLITE_DONE)return 0;
    features_broken=1;return -1;
}
/* Local connector account + explicit board allowlist. Peer signatures are
 * checked by the separate link service; public Telnet never trusts an origin
 * string as authentication. This path always runs in the board owner thread. */
static uint32_t link_users[CATALOG_MAX+1];
int board_link_enabled(uint32_t board,uint32_t user) { return board && board<=CATALOG_MAX && user && link_users[board]==user; }
int board_link_config(const char *path) {
    memset(link_users,0,sizeof(link_users));FILE *f=fopen(path,"r");if(!f)return 0;
    char line[256];int ok=1;
    while(fgets(line,sizeof(line),f)) {
        if(line[0]=='#' || line[0]=='\n' || line[0]=='\r')continue;
        unsigned board,user;char extra;
        if(sscanf(line,"%u %u %c",&board,&user,&extra)!=2 || !board || board>CATALOG_MAX || !user || user>MEMBER_MAX || link_users[board]) { ok=0;break; }
        link_users[board]=user;
    }
    if(ferror(f))ok=0;
    fclose(f);
    return ok;
}
static int link_done(uint32_t id) {
    sqlite3_stmt *s=feature_stmt("UPDATE link_articles SET done=1 WHERE local_id=?");if(!s)return 0;
    sqlite3_bind_int64(s,1,id);int ok=sqlite3_step(s)==SQLITE_DONE;sqlite3_finalize(s);return ok;
}
int link_repository_recover(void) {
    for(;;) {
        sqlite3_stmt *s=feature_stmt("SELECT local_id,kind,title,actor,board,parent,created,alias,body_hash FROM link_articles WHERE done=0 ORDER BY local_id LIMIT 1");if(!s)return 0;
        int rc=sqlite3_step(s),ok=1;
        if(rc!=SQLITE_ROW) { sqlite3_finalize(s);return rc==SQLITE_DONE; }
        uint32_t id=(uint32_t)sqlite3_column_int64(s,0),actor=(uint32_t)sqlite3_column_int64(s,3),board=(uint32_t)sqlite3_column_int64(s,4);
        uint32_t parent=(uint32_t)sqlite3_column_int64(s,5);uint64_t created=(uint64_t)sqlite3_column_int64(s,6);
        char kind[2],title[81],alias[33],expected_hash[65];
        snprintf(kind,sizeof(kind),"%s",sqlite3_column_text(s,1));snprintf(title,sizeof(title),"%s",sqlite3_column_text(s,2));
        snprintf(alias,sizeof(alias),"%s",sqlite3_column_text(s,7));snprintf(expected_hash,sizeof(expected_hash),"%s",sqlite3_column_text(s,8));
        sqlite3_finalize(s);
        if(id>article_count) {
            /* Intent committed but no article committed. Retry may recreate it. */
            if(!strchr("PCW",*kind) || id!=article_count+1)return 0;
            sqlite3_stmt *d=feature_stmt("DELETE FROM link_articles WHERE local_id=? AND done=0");if(!d)return 0;
            sqlite3_bind_int64(d,1,id);ok=sqlite3_step(d)==SQLITE_DONE;sqlite3_finalize(d);if(!ok)return 0;continue;
        }
        if(!id || !on_board(id,board))return 0;
        Article *a=article_at(id-1);
        if(*kind=='P') {
            char hash[65];const char *body=article_body(a);
            ok=!body_read_failed && a->parent==parent && a->created==created && !strcmp(a->author,alias) && !strcmp(a->title,title) && link_content_hash(body,hash) && !strcmp(hash,expected_hash);
        } else if(*kind=='C') {
            char hash[65];const char *body=article_body(a);
            ok=!body_read_failed && a->parent==parent && a->created==created && !strcmp(a->author,alias) && link_content_hash(body,hash) && !strcmp(hash,expected_hash) && !extras_removed(id);
            if(ok && strcmp(a->title,title)) { ok=extras_set('T',id,actor,title);if(ok) { strcpy(a->title,title);db_search_title(&article_db,id,title); } }
        } else if(*kind=='T') {
            if(extras_removed(id))ok=0;
            else if(strcmp(a->title,title)) { ok=extras_set('T',id,actor,title);if(ok) { strcpy(a->title,title);db_search_title(&article_db,id,title); } }
        } else if(*kind=='D' || *kind=='W') { if(!extras_removed(id))ok=extras_set('D',id,actor,""); }
        else ok=0;
        if(!ok || !link_done(id))return 0;
    }
}
static int link_open(void) {
    for(uint32_t b=1;b<=CATALOG_MAX;b++)if(link_users[b] && (!catalog_exists(b) || !members_name(link_users[b])))return 0;
    return feature_exec(LINK_ARTICLES_SCHEMA) && link_repository_recover();
}

LinkOutcome link_repository_health(void) {
    if(features_broken || policy_broken() || move_journal.broken || access_journal.broken)return LINK_BOARD_STORAGE;
    if(extras_broken() || profile_store_broken())return LINK_EXTRAS_STORAGE;
    if(members_broken())return LINK_MEMBER_STORAGE;
    return LINK_OK;
}
int link_repository_enabled(uint32_t board,uint32_t user) { return board_link_enabled(board,user); }
int link_repository_version(void) { return version; }
void link_repository_fail(void) { features_broken=1; }
int link_repository_on_board(uint32_t id,uint32_t board) { return on_board(id,board); }
int link_repository_removed(uint32_t id) { return extras_removed(id); }
int link_repository_next(uint32_t board,uint32_t start,LinkItem *out) {
    uint32_t id=start;while(id<=article_count && !on_board(id,board))id++;
    if(id>article_count)return 0;
    Article *a=article_at(id-1);out->id=id;out->parent=a->parent;out->created=a->created;
    out->state=extras_removed(id)?LINK_WITHDRAWN:unavailable(id)?LINK_HIDDEN:LINK_LIVE;
    snprintf(out->author,sizeof(out->author),"%s",a->author);snprintf(out->title,sizeof(out->title),"%s",a->title);
    strcpy(out->origin,"-");strcpy(out->key,"-");out->revision=0;
    sqlite3_stmt *s=feature_stmt("SELECT origin,article,revision FROM link_articles WHERE local_id=? AND done=1");if(!s)return -1;
    sqlite3_bind_int64(s,1,id);int rc=sqlite3_step(s);
    if(rc==SQLITE_ROW) {
        snprintf(out->origin,sizeof(out->origin),"%s",sqlite3_column_text(s,0));snprintf(out->key,sizeof(out->key),"%s",sqlite3_column_text(s,1));
        out->revision=(uint32_t)sqlite3_column_int64(s,2);
    }
    sqlite3_finalize(s);return (rc!=SQLITE_ROW && rc!=SQLITE_DONE) || body_read_failed?-1:1;
}
int link_repository_body(uint32_t id,char *out,size_t capacity,size_t *size) {
    const char *body=article_body(article_at(id-1));if(body_read_failed)return 0;
    *size=strlen(body);if(*size>=capacity)return 0;memcpy(out,body,*size+1);return 1;
}
int link_repository_lookup(uint32_t board,const char *origin,const char *key,LinkReference *out,int *done) {
    sqlite3_stmt *s=feature_stmt("SELECT local_id,revision,done FROM link_articles WHERE board=? AND origin=? AND article=?");if(!s)return 0;
    sqlite3_bind_int64(s,1,board);sqlite3_bind_text(s,2,origin,-1,SQLITE_STATIC);sqlite3_bind_text(s,3,key,-1,SQLITE_STATIC);
    int rc=sqlite3_step(s);
    if(rc==SQLITE_ROW) { out->id=(uint32_t)sqlite3_column_int64(s,0);out->revision=(uint32_t)sqlite3_column_int64(s,1);*done=sqlite3_column_int(s,2); }
    sqlite3_finalize(s);return rc==SQLITE_ROW || rc==SQLITE_DONE;
}
int link_repository_matches(uint32_t id,const LinkEvent *event,const char *alias,const char *hash) {
    Article *a=article_at(id-1);char existing_hash[65];const char *body=article_body(a);
    if(body_read_failed)return -1;
    return a->parent==event->parent && a->created==event->created && !strcmp(a->author,alias) && link_content_hash(body,existing_hash) && !strcmp(existing_hash,hash);
}
int link_repository_intent(const LinkActor *actor,const LinkEvent *event,uint32_t id,const char *alias,const char *hash) {
    sqlite3_stmt *s=feature_stmt("INSERT INTO link_articles VALUES(?,?,?,?,?,?,?,?,?,?,?,?,0,?) ON CONFLICT(board,origin,article) DO UPDATE SET revision=excluded.revision,kind=excluded.kind,title=excluded.title,done=0,actor=excluded.actor");if(!s)return 0;
    char kind[2]={event->kind,0};
    sqlite3_bind_int64(s,1,event->board);sqlite3_bind_text(s,2,event->origin,-1,SQLITE_STATIC);sqlite3_bind_text(s,3,event->key,-1,SQLITE_STATIC);
    sqlite3_bind_int64(s,4,id);sqlite3_bind_int64(s,5,event->revision);sqlite3_bind_text(s,6,kind,-1,SQLITE_STATIC);
    sqlite3_bind_text(s,7,event->author,-1,SQLITE_STATIC);sqlite3_bind_text(s,8,alias,-1,SQLITE_STATIC);sqlite3_bind_int64(s,9,(sqlite3_int64)event->created);
    sqlite3_bind_int64(s,10,event->parent);sqlite3_bind_text(s,11,event->title,-1,SQLITE_STATIC);sqlite3_bind_text(s,12,hash,-1,SQLITE_STATIC);sqlite3_bind_int64(s,13,actor->user);
    int ok=sqlite3_step(s)==SQLITE_DONE;sqlite3_finalize(s);return ok;
}
