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
static void access_report(uint32_t b,uint32_t cursor,char *out,size_t size) {
    uint32_t keep=policy_log_keep(b),retained=0;size_t oldest=access_count;
    if(keep)while(oldest && retained<keep) { oldest--;if((*access_at((uint32_t)(oldest))).board==b)retained++; }
    size_t index=cursor?cursor-1:access_count;if(index>access_count) { snprintf(out,size,"ERR command\r\n");return; }
    size_t used=(size_t)snprintf(out,size,"BOARDACCESS %u\r\n",b);unsigned shown=0,scanned=0;
    while(index>oldest && shown<8 && scanned++<256) { Access *a=&(*access_at((uint32_t)(--index)));if(a->board!=b)continue;
        used+=(size_t)snprintf(out+used,size-used,"ACCESSROW %u %u %u %c %llu\r\n",a->seq,a->article,a->user,a->kind,(unsigned long long)a->at);shown++;
    }
    snprintf(out+used,size-used,"END NEXT %zu\r\n",index>oldest?index+1:0);
}
static int board_usage(uint32_t board,uint32_t extra_count,uint64_t extra_bytes) {
    uint64_t bytes=extra_bytes;uint32_t count=extra_count;
    for(uint32_t i=1;i<=article_count;i++)if(on_board(i,board) && !extras_removed(i)) { count++;bytes+=(*article_at(i-1)).body_size; }
    return (!policy_max_articles(board) || count<=policy_max_articles(board)) && (!policy_max_bytes(board) || bytes<=policy_max_bytes(board));
}
static void board_features(const BoardRequest *q,BoardResult *r) {
    uint32_t b=q->number?q->number:q->board;
    if(q->action=='U') {
        if(!catalog_visible(b,q->user) || policy_locked(b,q->user)!=(int)q->cursor) { strcpy(r->text,"ERR board unavailable\r\n");return; }
        uint32_t revision=policy_unlock(q->cursor,q->password);if(!revision) { strcpy(r->text,"ERR authentication\r\n");return; }
        r->board_grant=q->cursor;r->board_revision=revision;r->service_action='2';r->service_number=b;return;
    }
    if(q->action=='G') {
        if(!on_board(q->number,q->board) || unavailable(q->number) || !catalog_read(q->board,q->user)) { strcpy(r->text,"ERR no article\r\n");return; }
        if(members_enabled() && !q->user) { strcpy(r->text,"ERR login required\r\n");return; }
        const char *body=article_body(article_at(q->number-1));if(failed || profile_set(q->user,PROFILE_BUFFER,body)!=1) { strcpy(r->text,"ERR article storage\r\n");return; }strcpy(r->text,"ARTICLE IMPORTED TO BUFFER\r\n");return;
    }
    if(!catalog_manage(b,q->user)) { strcpy(r->text,"ERR admin required\r\n");return; }
    if((strchr("SCWDMB",q->action) || (q->action=='O' && *q->body)) && !require_v5()) { strcpy(r->text,"ERR board policy storage\r\n");return; }
    if(q->action=='A') { access_report(b,q->cursor,r->text,sizeof(r->text));return; }
    if(q->action=='O') {
        if(!*q->body) { policy_report(b,r->text,sizeof(r->text));return; }
        if(!strncmp(q->body,"MENU ",5))for(uint32_t i=1;i<=article_count;i++)if(on_board(i,b)) { strcpy(r->text,"ERR board not empty\r\n");return; }
        int ok=policy_options(b,q->user,q->body);if(ok>0 && !access_prune(1)) { access_journal.broken=1;ok=0; }r->board_refresh=ok>0;strcpy(r->text,ok>0?"BOARD OPTIONS SAVED\r\n":ok<0?"ERR command\r\n":"ERR board policy storage\r\n");return;
    }
    if(q->action=='P') { r->service_action='3';r->service_number=b;r->board_grant=b;strcpy(r->text,"NEW PASSWORD\r\n");return; }
    if(q->action=='S' || q->action=='C') {
        if(!policy_password(b,q->user,q->action=='C'?"":q->new_password))strcpy(r->text,"ERR board policy storage\r\n");else { r->board_refresh=1;strcpy(r->text,"BOARD PASSWORD SAVED\r\n"); }return;
    }
    if(q->action=='I') { r->service_action='4';r->service_number=b;strcpy(r->draft_body,policy_info(b));strcpy(r->text,"BOARD INFO EDIT\r\n");return; }
    if(q->action=='W') { int ok=policy_information(b,q->user,q->body);r->service_action='5';r->service_number=ok>0?b:0;strcpy(r->text,ok>0?"BOARD INFO SAVED\r\n":ok<0?"ERR board info limit\r\n":"ERR board policy storage\r\n");return; }
    if(!moderation_admin(q->user)) { strcpy(r->text,"ERR admin required\r\n");return; }
    if(q->action=='D') {
        if(b==1) { strcpy(r->text,"ERR board not empty\r\n");return; }
        for(uint32_t i=1;i<=article_count;i++)if(on_board(i,b)) { strcpy(r->text,"ERR board not empty\r\n");return; }
        for(uint32_t i=1;i<=CATALOG_MAX;i++)if(catalog_exists(i) && catalog_parent(i)==b) { strcpy(r->text,"ERR board not empty\r\n");return; }
        if(!policy_delete(b,q->user) || !access_prune(1)) { access_journal.broken=1;strcpy(r->text,"ERR board policy storage\r\n"); }else { r->board_refresh=1;r->board=1;strcpy(r->text,"BOARD DELETED\r\n"); }return;
    }
    if(q->action=='M' || q->action=='B') {
        uint32_t root=q->cursor,to=q->audit_article;
        if(!on_board(root,b) || (q->action=='M' && (*article_at(root-1)).parent) || !catalog_exists(to) || policy_menu(to) || b==to) { strcpy(r->text,"ERR move target\r\n");return; }
        uint32_t count=0;uint64_t bytes=0;for(uint32_t i=root;i<=article_count;i++)if((q->action=='B'?in_branch(i,root):article_at(i-1)->root==root) && !extras_removed(i)) { count++;bytes+=(*article_at(i-1)).body_size; }
        if(!board_usage(to,count,bytes)) { strcpy(r->text,"ERR board full\r\n");return; }
        
        DurableRow row={0};row.kind=q->action=='B'?'S':'M';row.actor=q->user;row.target=root;snprintf(row.text,sizeof(row.text),"%u %u %u",b,to,article_count);
        if(!dj_append(&move_journal,&row) || !move_load(NULL,&row) || !moves_at(article_count)) { move_journal.broken=1;strcpy(r->text,"ERR board policy storage\r\n");return; }
        r->board_refresh=1;snprintf(r->text,sizeof(r->text),"THREAD MOVED %u %u\r\n",root,to);return;
    }
    strcpy(r->text,"ERR command\r\n");
}
