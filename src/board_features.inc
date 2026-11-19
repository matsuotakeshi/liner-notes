#include "storage_sqlite.h"
/* Included by board.c: state and journals stay on the single board worker. */
typedef struct { uint32_t root,from,to,through;char kind; } BoardMove;
static DbIndex moves_db,accesses_db;
static BoardMove *move_at(uint32_t id) { if(!moves_db.sql)db_open(&moves_db,NULL,"",sizeof(BoardMove));return db_at(&moves_db,id); }static size_t move_count,move_replayed;
static Durable move_journal={-1,0,0},access_journal={-1,0,0};
typedef struct { uint32_t seq,board,article,user;uint64_t at;char kind; } Access;
static Access *access_at(uint32_t id) { if(!accesses_db.sql)db_open(&accesses_db,NULL,"",sizeof(Access));return db_at(&accesses_db,id); }static size_t access_count;
static int move_load(void *unused,const DurableRow *r) {
    (void)unused;BoardMove m={0};int n=0;char canonical[80];m.root=r->target;m.kind=r->kind;
    if((r->kind!='M' && r->kind!='S') || !members_name(r->actor) || !m.root || sscanf(r->text,"%u %u %u%n",&m.from,&m.to,&m.through,&n)!=3 || r->text[n] || !catalog_known(m.from) || !catalog_known(m.to) || m.from==m.to || m.root>m.through || m.through>CONTENT_ID_MAX || (move_count && (*move_at((uint32_t)(move_count-1))).through>m.through))return 0;
    snprintf(canonical,sizeof(canonical),"%u %u %u",m.from,m.to,m.through);if(strcmp(canonical,r->text))return 0;
    
    (*move_at((uint32_t)(move_count++)))=m;return 1;
}
static void index_article(uint32_t id);
static void rebuild_links(uint32_t through) {
    for(uint32_t i=1;i<=through;i++) { Article *a=article_at(i-1);a->root=a->depth=a->child=a->last_child=a->next=a->prev=a->replies=a->latest=0; }
    for(uint32_t i=1;i<=through;i++)index_article(i);
}
static int in_branch(uint32_t id,uint32_t root) {
    while(id>=root) { if(id==root)return 1;id=article_at(id-1)->parent; }return 0;
}
static int moves_at(uint32_t through) {
    while(move_replayed<move_count && (*move_at((uint32_t)(move_replayed))).through<=through) {
        BoardMove *m=&(*move_at((uint32_t)(move_replayed)));if(m->through!=through || (m->kind=='M' && (*article_at(m->root-1)).parent) || (*article_at(m->root-1)).board!=m->from)return 0;
        for(uint32_t i=m->root;i<=through;i++)if(m->kind=='S'?in_branch(i,m->root):article_at(i-1)->root==m->root) {
            if(article_at(i-1)->board!=m->from)return 0;
            article_at(i-1)->board=m->to;
            char sql[160];snprintf(sql,sizeof(sql),"INSERT OR IGNORE INTO locations VALUES(%u,%u);INSERT OR IGNORE INTO locations VALUES(%u,%u)",i,m->from,i,m->to);db_exec(&article_db,sql);
        }
        if(m->kind=='S') { article_at(m->root-1)->parent=0;rebuild_links(through); }
        move_replayed++;
    }return 1;
}
static int article_was_on(uint32_t id,uint32_t b) {
    if(!id || id>article_count)return 0;
    if((*article_at(id-1)).origin==b)return 1;
    for(size_t i=0;i<move_count;i++)if((*move_at((uint32_t)(i))).through>=id && (*move_at((uint32_t)(i))).root==(*article_at(id-1)).root && ((*move_at((uint32_t)(i))).from==b || (*move_at((uint32_t)(i))).to==b))return 1;
    sqlite3_stmt *s=NULL;if(sqlite3_prepare_v2(article_db.sql,"SELECT 1 FROM locations WHERE id=? AND board=?",-1,&s,NULL)!=SQLITE_OK)return 0;
    sqlite3_bind_int(s,1,(int)id);sqlite3_bind_int(s,2,(int)b);int found=sqlite3_step(s)==SQLITE_ROW;sqlite3_finalize(s);return found;
}
static int access_load(void *unused,const DurableRow *r) {
    (void)unused;uint32_t b;int n=0;char canonical[20];
    if((r->kind!='R' && r->kind!='P') || r->actor>MEMBER_MAX || !r->target || sscanf(r->text,"%u%n",&b,&n)!=1 || r->text[n] || !catalog_known(b) || r->time>253402300799ULL)return 0;
    snprintf(canonical,sizeof(canonical),"%u",b);if(strcmp(canonical,r->text))return 0;
    
    (*access_at((uint32_t)(access_count++)))=(Access){r->sequence,b,r->target,r->actor,r->time,r->kind};return 1;
}
static char access_path[4096];static int access_changed;
/* Publish a fully synced retained snapshot. State journals are never pruned.
 * The board-store lock excludes a second writer during replacement. */
static int access_prune(int force) {
    uint32_t counts[CATALOG_MAX+1]={0};size_t kept=0;
    unsigned char *retain=calloc(access_count?access_count:1,1);if(!retain)return 0;
    for(size_t i=access_count;i>0;i--) { Access *a=&(*access_at((uint32_t)(i-1)));uint32_t keep=policy_deleted(a->board)?0:policy_log_keep(a->board);
        if(counts[a->board]<keep) { counts[a->board]++;retain[i-1]=1;kept++; }
    }
    if(kept==access_count || (!force && access_count-kept<64)) { free(retain);return 1; }
    unsigned char random[8];char temporary[4160],suffix[17];if(RAND_bytes(random,sizeof(random))!=1) { free(retain);return 0; }
    for(unsigned i=0;i<8;i++)snprintf(suffix+i*2,3,"%02x",random[i]);
    snprintf(temporary,sizeof(temporary),"%s.retaining-%s",access_path,suffix);
#ifdef _WIN32
    int fd=_open(temporary,_O_RDWR|_O_CREAT|_O_EXCL|_O_BINARY,_S_IREAD|_S_IWRITE);
#else
    int fd=open(temporary,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
#endif
    if(fd<0) { free(retain);return 0; }
    Durable next={fd,0,0};int ok=1;
#ifdef _WIN32
    OVERLAPPED ov={0};if(!LockFileEx((HANDLE)_get_osfhandle(fd),LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&ov))ok=0;
#else
    struct flock lock={0};lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;if(storage_lock(fd,&lock))ok=0;
#endif
    unsigned char magic[]="LNBACC01";if(ok && dj_io(fd,magic,8,1)!=8)ok=0;
    for(size_t i=0;i<access_count && ok;i++)if(retain[i]) {
        Access *a=&(*access_at((uint32_t)(i)));unsigned char row[DJ_ROW]={0};char board[20];int n=snprintf(board,sizeof(board),"%u",a->board);
        row[0]=(unsigned char)a->kind;dj_put(row+4,++next.sequence);dj_put(row+8,a->user);dj_put(row+12,a->article);dj_put(row+16,(uint32_t)a->at);dj_put(row+20,(uint32_t)(a->at>>32));dj_put(row+24,(uint32_t)n);memcpy(row+28,board,(size_t)n);dj_put(row+DJ_ROW-4,dj_sum(row));
        if(dj_io(fd,row,DJ_ROW,1)!=DJ_ROW)ok=0;
    }
    if(ok && DJ_SYNC(fd))ok=0;
    if(!ok) { dj_close(&next);free(retain);return 0; } /* Keep failed temp for inspection. */
#ifdef _WIN32
    /* Windows cannot replace an open CRT target. The main board lock is still held. */
    dj_close(&access_journal);dj_close(&next);
    if(!MoveFileExA(temporary,access_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) { free(retain);access_journal.broken=1;return 0; }
    /* Reload exactly the new snapshot and reacquire its exclusive lock. */
    access_count=0;free(retain);access_changed=1;
    return dj_open(&access_journal,access_path,"LNBACC01",access_load,NULL);
#else
    if(storage_rename(temporary,access_path)) { dj_close(&next);free(retain);return 0; }
    dj_close(&access_journal);if(storage_database()) { uint32_t sequence=next.sequence;dj_close(&next);next.fd=storage_open(access_path,O_RDWR,0600);next.sequence=sequence;if(next.fd<0) { free(retain);return 0; } }access_journal=next;
    size_t n=0;for(size_t i=0;i<access_count;i++)if(retain[i]) { (*access_at((uint32_t)(n)))=(*access_at((uint32_t)(i)));(*access_at((uint32_t)(n))).seq=(uint32_t)n+1;n++; }
    access_count=n;free(retain);access_changed=1;
    char directory[4096];strcpy(directory,access_path);char *slash=strrchr(directory,'/');if(slash) { if(slash==directory)slash[1]=0;else *slash=0; }else strcpy(directory,".");
    int dir=open(directory,O_RDONLY);if(dir<0)return 0;int error=storage_sync(dir);storage_close(dir);return !error;
#endif
}
static int access_record(char kind,uint32_t article,uint32_t user) {
    uint32_t b=(*article_at(article-1)).board;if(!policy_log_keep(b))return 1;
    
    DurableRow r={0};r.kind=kind;r.target=article;r.actor=user;snprintf(r.text,sizeof(r.text),"%u",b);
    if(!dj_append(&access_journal,&r) || !access_load(NULL,&r) || !access_prune(0)) { access_journal.broken=1;return 0; }return 1;
}
int board_policy_repository_broken(void) { return access_journal.broken || move_journal.broken; }
int board_policy_repository_prepare(void) { return require_v5(); }
int board_policy_repository_has_articles(uint32_t board) {
    for(uint32_t i=1;i<=article_count;i++)if(on_board(i,board))return 1;
    return 0;
}
int board_policy_repository_prune(void) {
    if(access_prune(1))return 1;
    access_journal.broken=1;return 0;
}
uint32_t board_policy_repository_access_count(void) { return (uint32_t)access_count; }
int board_policy_repository_access_get(uint32_t index,BoardAccessRecord *out) {
    memset(out,0,sizeof(*out));
    if(board_policy_repository_broken() || index>=access_count)return 0;
    const Access *row=access_at(index);
    *out=(BoardAccessRecord){row->seq,row->board,row->article,row->user,row->at,row->kind};return 1;
}
static void board_policy_result(CatalogOutcome result,BoardResult *r) {
    const char *error="ERR board policy storage\r\n";
    switch(result) {
    case CATALOG_CORE_OK:return;
    case CATALOG_CORE_LOGIN_REQUIRED:case CATALOG_CORE_DENIED:error="ERR admin required\r\n";break;
    case CATALOG_CORE_REVOKED:r->revoked=1;error="ERR session revoked\r\n";break;
    case CATALOG_CORE_INVALID:case CATALOG_CORE_CURSOR:error="ERR command\r\n";break;
    case CATALOG_CORE_NOT_EMPTY:error="ERR board not empty\r\n";break;
    case CATALOG_CORE_INFO_LIMIT:error="ERR board info limit\r\n";break;
    case CATALOG_CORE_MEMBER_STORAGE:error="ERR member storage unavailable\r\n";break;
    case CATALOG_CORE_MODERATION_STORAGE:error="ERR moderation storage unavailable\r\n";break;
    case CATALOG_CORE_STORAGE:error="ERR catalog storage\r\n";break;
    case CATALOG_CORE_POLICY_STORAGE:case CATALOG_CORE_BUFFER:break;
    }
    r->feedback.failed=1;snprintf(r->text,sizeof(r->text),"%s",error);
}
static void board_policy_execute(const BoardRequest *q,uint32_t b,BoardResult *r) {
    CatalogActor actor={q->user,q->user_epoch};CatalogOutcome result;
    if(q->action=='A') {
        BoardAccessPage page;result=catalog_access_page(&actor,b,q->cursor,&page);
        if(result!=CATALOG_CORE_OK) { board_policy_result(result,r);return; }
        size_t used=(size_t)snprintf(r->text,sizeof(r->text),"BOARDACCESS %u\r\n",b);
        for(unsigned i=0;i<page.count;i++) {
            const BoardAccessRecord *row=&page.rows[i];
            used+=(size_t)snprintf(r->text+used,sizeof(r->text)-used,"ACCESSROW %u %u %u %c %llu\r\n",
                                  row->sequence,row->article,row->user,row->kind,(unsigned long long)row->time);
        }
        (r->feedback.page_found=1,r->feedback.page_next=page.next,snprintf(r->text+used,sizeof(r->text)-used,"END NEXT %u\r\n",page.next));return;
    }
    if(q->action=='O') {
        if(!*q->body) {
            BoardPolicySettings settings;result=catalog_policy_get(&actor,b,&settings);
            if(result!=CATALOG_CORE_OK) { board_policy_result(result,r);return; }
            snprintf(r->text,sizeof(r->text),"BOARDOPTIONS %u %s ORDER %u ARTICLES %u BYTES %llu LOG %u PASSWORD %s\r\n",
                     b,settings.menu?"MENU":"BOARD",settings.order,settings.max_articles,
                     (unsigned long long)settings.max_bytes,settings.log_keep,settings.password_set?"ON":"OFF");return;
        }
        result=catalog_policy_set(&actor,b,q->body);
        if(result==CATALOG_CORE_OK) { r->board_refresh=1;strcpy(r->text,"BOARD OPTIONS SAVED\r\n"); }
        else board_policy_result(result,r);
        return;
    }
    if(q->action=='P') {
        result=catalog_password_prepare(&actor,b);
        if(result!=CATALOG_CORE_OK) { board_policy_result(result,r);return; }
        r->service_action='3';r->service_number=b;r->board_grant=b;strcpy(r->text,"NEW PASSWORD\r\n");return;
    }
    if(q->action=='S' || q->action=='C') {
        result=catalog_password_set(&actor,b,q->action=='C'?"":q->new_password);
        if(result==CATALOG_CORE_OK) { r->board_refresh=1;strcpy(r->text,"BOARD PASSWORD SAVED\r\n"); }
        else board_policy_result(result,r);
        return;
    }
    if(q->action=='I') {
        result=catalog_information_get(&actor,b,r->draft_body,sizeof(r->draft_body));
        if(result!=CATALOG_CORE_OK) { board_policy_result(result,r);return; }
        r->service_action='4';r->service_number=b;strcpy(r->text,"BOARD INFO EDIT\r\n");return;
    }
    int attempted=0;result=catalog_information_set(&actor,b,q->body,&attempted);
    if(attempted) { r->service_action='5';r->service_number=result==CATALOG_CORE_OK?b:0; }
    if(result==CATALOG_CORE_OK)strcpy(r->text,"BOARD INFO SAVED\r\n");
    else board_policy_result(result,r);
}
uint32_t board_topology_repository_count(void) { return article_count; }
int board_topology_repository_article(uint32_t id,BoardTopologyArticle *out) {
    memset(out,0,sizeof(*out));
    if(!id || id>article_count || board_policy_repository_broken() || extras_broken())return 0;
    const Article *a=article_at(id-1);
    *out=(BoardTopologyArticle){a->board,a->parent,a->root,a->body_size,extras_removed(id)};return 1;
}
int board_topology_repository_delete(uint32_t board,uint32_t actor) {
    if(!policy_delete(board,actor) || !access_prune(1)) { access_journal.broken=1;return 0; }
    return 1;
}
int board_topology_repository_move(uint32_t actor,uint32_t from,uint32_t to,
                                   uint32_t root,uint32_t through,int branch) {
    if(through!=article_count)return 0;
    DurableRow row={0};row.kind=branch?'S':'M';row.actor=actor;row.target=root;
    snprintf(row.text,sizeof(row.text),"%u %u %u",from,to,through);
    if(!dj_append(&move_journal,&row) || !move_load(NULL,&row) || !moves_at(through)) {
        move_journal.broken=1;return 0;
    }
    return 1;
}
static int board_usage(uint32_t board,uint32_t extra_count,uint64_t extra_bytes) {
    return board_topology_capacity(board,extra_count,extra_bytes)>0;
}
static void board_topology_result(BoardTopologyOutcome result,BoardResult *r) {
    const char *error="ERR command\r\n";
    switch(result) {
    case TOPOLOGY_OK:return;
    case TOPOLOGY_DENIED:error="ERR admin required\r\n";break;
    case TOPOLOGY_REVOKED:r->revoked=1;error="ERR session revoked\r\n";break;
    case TOPOLOGY_MEMBER_STORAGE:error="ERR member storage unavailable\r\n";break;
    case TOPOLOGY_MODERATION_STORAGE:error="ERR moderation storage unavailable\r\n";break;
    case TOPOLOGY_POLICY_STORAGE:error="ERR board policy storage\r\n";break;
    case TOPOLOGY_CATALOG_STORAGE:error="ERR catalog storage\r\n";break;
    case TOPOLOGY_EXTRAS_STORAGE:error="ERR extras storage\r\n";break;
    case TOPOLOGY_SETTINGS_STORAGE:error="ERR settings storage\r\n";break;
    case TOPOLOGY_NOT_EMPTY:error="ERR board not empty\r\n";break;
    case TOPOLOGY_MOVE_TARGET:error="ERR move target\r\n";break;
    case TOPOLOGY_FULL:error="ERR board full\r\n";break;
    case TOPOLOGY_INVALID:break;
    }
    r->feedback.failed=1;snprintf(r->text,sizeof(r->text),"%s",error);
}
static void board_topology_execute(const BoardRequest *q,uint32_t board,int cascade,BoardResult *r) {
    CatalogActor actor={q->user,q->user_epoch};
    BoardTopologyOutcome result=q->action=='D'?board_topology_delete(&actor,board,cascade):
        board_topology_move(&actor,board,q->cursor,q->audit_article,q->action=='B');
    if(result!=TOPOLOGY_OK) { board_topology_result(result,r);return; }
    r->board_refresh=1;
    if(q->action=='D') {
        r->board=1;strcpy(r->text,cascade?"BOARD TREE DELETED\r\n":"BOARD DELETED\r\n");
    }else snprintf(r->text,sizeof(r->text),"THREAD MOVED %u %u\r\n",q->cursor,q->audit_article);
}
static void board_features(const BoardRequest *q,BoardResult *r) {
    uint32_t b=q->number?q->number:q->board;
    if(q->action=='U') {
        if(!catalog_visible(b,q->user) || policy_locked(b,q->user)!=(int)q->cursor) { (r->feedback.failed=1,strcpy(r->text,"ERR board unavailable\r\n"));return; }
        uint32_t revision=policy_unlock(q->cursor,q->password);if(!revision) { (r->feedback.failed=1,strcpy(r->text,"ERR authentication\r\n"));return; }
        r->board_grant=q->cursor;r->board_revision=revision;r->service_action='2';r->service_number=b;return;
    }
    if(q->action=='G') {
        ArticleActor actor={q->user,q->user_epoch,q->board_grants};
        ArticleOutcome result=article_import_buffer(&actor,q->board,q->number,r->draft_body,sizeof(r->draft_body));
        if(result==ARTICLE_OK)strcpy(r->text,"ARTICLE IMPORTED TO BUFFER\r\n");
        else if(result==ARTICLE_NOT_FOUND)(r->feedback.failed=1,strcpy(r->text,"ERR no article\r\n"));
        else if(result==ARTICLE_LOGIN_REQUIRED)(r->feedback.failed=1,strcpy(r->text,"ERR login required\r\n"));
        else if(result==ARTICLE_REVOKED) { r->revoked=1;(r->feedback.failed=1,strcpy(r->text,"ERR session revoked\r\n")); }
        else (r->feedback.failed=1,strcpy(r->text,"ERR article storage\r\n"));
        return;
    }
    if(q->action && strchr("AOPSCIW",q->action)) { board_policy_execute(q,b,r);return; }
    if(q->action && strchr("DMB",q->action)) { board_topology_execute(q,b,0,r);return; }
    if(!catalog_manage(b,q->user) || !moderation_admin(q->user)) { (r->feedback.failed=1,strcpy(r->text,"ERR admin required\r\n"));return; }
    (r->feedback.failed=1,strcpy(r->text,"ERR command\r\n"));
}
