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
    if(ferror(f))ok=0;fclose(f);return ok;
}
static int link_hex(const char *text) { if(strlen(text)!=64)return 0;for(unsigned i=0;i<64;i++)if(!strchr("0123456789abcdef",text[i]))return 0;return 1; }
static int link_hash(const char *body,char out[65]) {
    unsigned char digest[32];unsigned size=0;
    if(!EVP_Digest(body,strlen(body),digest,&size,EVP_sha256(),NULL) || size!=32)return 0;
    for(unsigned i=0;i<32;i++)snprintf(out+i*2,3,"%02x",digest[i]);return 1;
}
static int link_done(uint32_t id) {
    sqlite3_stmt *s=feature_stmt("UPDATE link_articles SET done=1 WHERE local_id=?");if(!s)return 0;
    sqlite3_bind_int64(s,1,id);int ok=sqlite3_step(s)==SQLITE_DONE;sqlite3_finalize(s);return ok;
}
static int link_recover(void) {
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
            ok=!body_read_failed && a->parent==parent && a->created==created && !strcmp(a->author,alias) && !strcmp(a->title,title) && link_hash(body,hash) && !strcmp(hash,expected_hash);
        } else if(*kind=='C') {
            char hash[65];const char *body=article_body(a);
            ok=!body_read_failed && a->parent==parent && a->created==created && !strcmp(a->author,alias) && link_hash(body,hash) && !strcmp(hash,expected_hash) && !extras_removed(id);
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
    return feature_exec(LINK_ARTICLES_SCHEMA) && link_recover();
}
static void link_execute(const BoardRequest *q,BoardResult *r) {
    if(!board_link_enabled(q->board,q->user) || !members_name(q->user) || !catalog_read(q->board,q->user)) { strcpy(r->text,"ERR link denied\r\n");return; }
    if(q->action=='S' || q->action=='M' || q->action=='B') {
        uint32_t id=q->cursor?q->cursor:1;size_t used=0;
        for(unsigned item=0;item<(q->action=='B'?32U:1U);item++) {
        while(id<=article_count && !on_board(id,q->board))id++;
        if(id>article_count) { snprintf(r->text+used,sizeof(r->text)-used,"LINK END\r\n");return; }
        Article *a=article_at(id-1);const char *state=extras_removed(id)?"WITHDRAWN":unavailable(id)?"HIDDEN":"LIVE";
        sqlite3_stmt *s=feature_stmt("SELECT origin,article,revision FROM link_articles WHERE local_id=? AND done=1");if(!s)goto storage;
        sqlite3_bind_int64(s,1,id);int rc=sqlite3_step(s);char origin[65]="-",key[65]="-";uint32_t revision=0;
        if(rc==SQLITE_ROW) { snprintf(origin,sizeof(origin),"%s",sqlite3_column_text(s,0));snprintf(key,sizeof(key),"%s",sqlite3_column_text(s,1));revision=(uint32_t)sqlite3_column_int64(s,2); }
        sqlite3_finalize(s);if(rc!=SQLITE_ROW && rc!=SQLITE_DONE)goto storage;
        const char *body=q->action=='S' && !strcmp(state,"LIVE")?article_body(a):"";if(body_read_failed)goto storage;
        int size=snprintf(r->text+used,sizeof(r->text)-used,"LINK ITEM %u %u %llu %s %s %s %u\r\nAUTHOR %s\r\nTITLE %s\r\nBODY %zu\r\n%s",id,a->parent,(unsigned long long)a->created,state,origin,key,revision,a->author,!strcmp(state,"LIVE")?a->title:"",strlen(body),body);
        if(size<0 || (size_t)size>=sizeof(r->text)-used)goto storage;used+=(size_t)size;id++;
        }return;
    }
    if(!link_hex(q->link_origin) || !link_hex(q->link_article)) { strcpy(r->text,"ERR link identity\r\n");return; }
    sqlite3_stmt *s=feature_stmt("SELECT local_id,revision,done FROM link_articles WHERE board=? AND origin=? AND article=?");if(!s)goto storage;
    sqlite3_bind_int64(s,1,q->board);sqlite3_bind_text(s,2,q->link_origin,-1,SQLITE_STATIC);sqlite3_bind_text(s,3,q->link_article,-1,SQLITE_STATIC);
    int rc=sqlite3_step(s);uint32_t id=rc==SQLITE_ROW?(uint32_t)sqlite3_column_int64(s,0):0,revision=rc==SQLITE_ROW?(uint32_t)sqlite3_column_int64(s,1):0;int done=rc==SQLITE_ROW?sqlite3_column_int(s,2):1;
    sqlite3_finalize(s);if((rc!=SQLITE_ROW && rc!=SQLITE_DONE) || !done)goto storage;
    if(q->action=='L') { snprintf(r->text,sizeof(r->text),"LINK LOOKUP %u %u\r\n",id,revision);return; }
    if(!catalog_write(q->board,q->user) || policy_menu(q->board) || version!=4) { strcpy(r->text,"ERR link posting denied\r\n");return; }
    if(!q->cursor || q->cursor>CONTENT_ID_MAX) { strcpy(r->text,"ERR link revision\r\n");return; }
    if(id && q->cursor<=revision) { snprintf(r->text,sizeof(r->text),"LINK APPLIED %u %u\r\n",id,revision);return; }
    int checkpoint=q->action=='C' || q->action=='W';
    if((!checkpoint && q->cursor!=revision+1) || (!id && q->action!='P' && !checkpoint) || (id && q->action=='P')) { strcpy(r->text,"ERR link missing predecessor\r\n");return; }
    if(!strchr("PTDCW",q->action)) { strcpy(r->text,"ERR link action\r\n");return; }
    if(id && ((extras_removed(id) && q->action!='W') || !on_board(id,q->board))) { strcpy(r->text,"ERR link article unavailable\r\n");return; }
    if((q->action=='P'||q->action=='T'||q->action=='C') && !extra_text(q->title,80,0)) { strcpy(r->text,"ERR link title\r\n");return; }
    /* A missing withdrawn parent is represented without its former content. */
    BoardRequest bootstrap;int create=!id;
    if(create && checkpoint) {
        bootstrap=*q;
        if(q->action=='W') { strcpy(bootstrap.title,"Withdrawn");strcpy(bootstrap.body,"[Article withdrawn at origin]\r\n"); }
        q=&bootstrap;
    }
    char alias[33],hash[65];
    if(!link_hash(q->body,hash))goto storage;
    size_t author_size=strlen(q->username);if(author_size>22) { author_size=22;while(author_size && ((unsigned char)q->username[author_size]&0xc0)==0x80)author_size--; }
    snprintf(alias,sizeof(alias),"@%.8s:%.*s",q->link_origin,(int)author_size,q->username);
    if(!create && q->action=='C') {
        Article *existing=article_at(id-1);char existing_hash[65];const char *existing_body=article_body(existing);
        if(body_read_failed)goto storage;
        if(existing->parent!=q->number || existing->created!=q->since || strcmp(existing->author,alias) || !link_hash(existing_body,existing_hash) || strcmp(existing_hash,hash)) { strcpy(r->text,"ERR link checkpoint conflicts with immutable article\r\n");return; }
    }
    if(create) {
        if(q->number && !on_board(q->number,q->board)) { strcpy(r->text,"ERR link parent unavailable\r\n");return; }
        post_article(q,r,alias,q->since,1);if(!r->selected)return;id=r->selected;r->selected=0;
    }
    s=feature_stmt("INSERT INTO link_articles VALUES(?,?,?,?,?,?,?,?,?,?,?,?,0,?) ON CONFLICT(board,origin,article) DO UPDATE SET revision=excluded.revision,kind=excluded.kind,title=excluded.title,done=0,actor=excluded.actor");if(!s)goto storage;
    char kind[2]={(char)q->action,0};
    sqlite3_bind_int64(s,1,q->board);sqlite3_bind_text(s,2,q->link_origin,-1,SQLITE_STATIC);sqlite3_bind_text(s,3,q->link_article,-1,SQLITE_STATIC);sqlite3_bind_int64(s,4,id);sqlite3_bind_int64(s,5,q->cursor);sqlite3_bind_text(s,6,kind,-1,SQLITE_STATIC);sqlite3_bind_text(s,7,q->username,-1,SQLITE_STATIC);sqlite3_bind_text(s,8,alias,-1,SQLITE_STATIC);sqlite3_bind_int64(s,9,(sqlite3_int64)q->since);sqlite3_bind_int64(s,10,q->number);sqlite3_bind_text(s,11,q->title,-1,SQLITE_STATIC);sqlite3_bind_text(s,12,hash,-1,SQLITE_STATIC);sqlite3_bind_int64(s,13,q->user);
    int ok=sqlite3_step(s)==SQLITE_DONE;sqlite3_finalize(s);if(!ok)goto storage;
    if(create) { post_article(q,r,alias,q->since,0);if(r->selected!=id)goto storage; }
    if(!link_recover())goto storage;
    snprintf(r->text,sizeof(r->text),"LINK APPLIED %u %u\r\n",id,q->cursor);return;
storage:features_broken=1;strcpy(r->text,"ERR link storage; restart inspection required\r\n");
}
