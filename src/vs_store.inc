/* VS settings use the existing board-worker-owned features database.
 * kind 6 is isolated from every other profile. */
#include "vs_core.h"

static int vs_setting(uint32_t user,const char *key,uint64_t *stamp) {
    return member_settings_repository_check_get(user,MEMBER_CHECK_VS,key,stamp);
}

static int vs_put(uint32_t user,const char *key,uint64_t stamp) {
    sqlite3_stmt *s=feature_stmt("INSERT INTO news_scoped(user,kind,stamp,scope) VALUES(?1,?2,?3,?4)"
        " ON CONFLICT(user,kind,scope) DO UPDATE SET stamp=excluded.stamp");
    int ok=s && sqlite3_bind_int64(s,1,user)==SQLITE_OK &&
        sqlite3_bind_int(s,2,MEMBER_CHECK_VS)==SQLITE_OK &&
        sqlite3_bind_int64(s,3,(sqlite3_int64)stamp)==SQLITE_OK &&
        sqlite3_bind_text(s,4,key,-1,SQLITE_STATIC)==SQLITE_OK && sqlite3_step(s)==SQLITE_DONE;
    return sqlite3_finalize(s)==SQLITE_OK && ok;
}

static void vs_operation(const BoardRequest *q,BoardResult *r) {
    ArticleActor actor={q->user,q->user_epoch,q->board_grants};
    ArticleOutcome result=ARTICLE_OK;
    r->service_action=q->action;
    if(q->action==VS_PAGE) {
        ArticlePage page;
        if(q->scope==1)result=article_browse_notes(&actor,q->board,0,q->cursor,q->descending,0,NULL,&page);
        else if(q->number)result=article_vs_thread(&actor,q->board,q->number,q->cursor,
                                                  q->descending,q->since,q->until,&page);
        else if(q->until)result=article_news_filtered(&actor,q->board,ARTICLE_HERE,
                                                      q->cursor,q->since,q->until,0,&page);
        else result=article_browse_board(&actor,q->board,q->cursor,q->descending,&page);
        if(result==ARTICLE_OK)OPERATION_PAGE(r,ARTICLES,articles,page);
        else post_result(result,r);
        return;
    }
    MemberActor own={q->user,q->user_epoch};
    MemberCheckWindow window;
    MemberOutcome valid=member_core_check_begin(&own,MEMBER_CHECK_VS,"",&window);
    if(valid!=MEMBER_CORE_OK) { member_result(valid,r);return; }
    if(q->action==VS_MODE_GET) {
        uint64_t mode=0;
        if(!vs_setting(q->user,"mode",&mode) || mode>1)r->feedback.failed=1;
        else r->service_number=(uint32_t)mode;
        return;
    }
    if(q->action==VS_MODE_SET) {
        if(q->number>1 || !vs_put(q->user,"mode",q->number))r->feedback.failed=1;
        else r->service_number=q->number;
        return;
    }
    if(q->action==VS_TARGETS) {
        MmmScanList list={0};
        for(uint32_t b=q->cursor?q->cursor:1;b<=CATALOG_MAX;b++) {
            if(!catalog_exists(b) || (q->number && !catalog_under(b,q->number)))continue;
            ArticleBoardEntry entry;
            result=article_enter_board(&actor,b,&entry);
            if(result==ARTICLE_BOARD_DENIED || (result==ARTICLE_OK && (entry.password_board || entry.menu)))continue;
            if(result!=ARTICLE_OK) { post_result(result,r);return; }
            char key[24];uint64_t masked=0;
            snprintf(key,sizeof(key),"mask:%u",b);
            if(!vs_setting(q->user,key,&masked)) { r->feedback.failed=1;return; }
            if(masked && !q->scope)continue;
            if(list.count==MMM_SCAN_MAX) { r->service_next=b;break; }
            list.rows[list.count].board=b;
            strcpy(list.rows[list.count++].title,entry.title);
        }
        OPERATION_PAGE(r,MMM_SCANLIST,mmm_scanlist,list);
        return;
    }

    uint32_t board=q->number;
    if(board) {
        ArticleBoardEntry entry;
        result=article_enter_board(&actor,board,&entry);
        if(result!=ARTICLE_OK || entry.password_board) {
            post_result(result==ARTICLE_OK?ARTICLE_BOARD_DENIED:result,r);return;
        }
    }
    if(q->action==VS_MASK_SET && board && (q->scope==0 || q->scope==1)) {
        char key[24];snprintf(key,sizeof(key),"mask:%u",board);
        if(!vs_put(q->user,key,(uint64_t)q->scope))r->feedback.failed=1;
        return;
    }

    char key[24]="";
    if(board)snprintf(key,sizeof(key),"%u:0",board);
    if(q->action==VS_WINDOW) {
        uint64_t since=0,explicit_position=0;
        if(board) {
            if(!vs_setting(q->user,key,&since)) { r->feedback.failed=1;return; }
            snprintf(key,sizeof(key),"position:%u",board);
            if(!vs_setting(q->user,key,&explicit_position)) { r->feedback.failed=1;return; }
        }
        r->since=explicit_position?since:window.since;r->until=window.until;
    } else if((q->action==VS_COMMIT && board) || q->action==VS_POINTER) {
        uint64_t stamp=q->until;
        if(stamp>window.until) { r->feedback.failed=1;return; }
        if(!feature_exec("SAVEPOINT vs_pointer")) { r->feedback.failed=1;return; }
        int ok=1;
        if(!board) {
            sqlite3_stmt *s=feature_stmt("DELETE FROM news_scoped WHERE user=?1 AND kind=?2"
                " AND (scope GLOB '*:0' OR scope GLOB 'position:*')");
            int cleared=s && sqlite3_bind_int64(s,1,q->user)==SQLITE_OK &&
                sqlite3_bind_int(s,2,MEMBER_CHECK_VS)==SQLITE_OK && sqlite3_step(s)==SQLITE_DONE;
            ok=sqlite3_finalize(s)==SQLITE_OK && cleared &&
                member_core_sequencer_set(&own,MEMBER_CHECK_VS,stamp)==MEMBER_CORE_OK;
        } else {
            for(uint32_t b=q->action==VS_POINTER && q->scope?1:board;b<=CATALOG_MAX && ok;b++) {
                if(b!=board && (q->action==VS_COMMIT || !q->scope || !catalog_exists(b) || !catalog_under(b,board)))continue;
                ArticleBoardEntry entry;
                result=article_enter_board(&actor,b,&entry);
                if(result==ARTICLE_BOARD_DENIED || (result==ARTICLE_OK && entry.password_board))continue;
                if(result!=ARTICLE_OK) { ok=0;break; }
                snprintf(key,sizeof(key),"%u:0",b);
                if(q->action==VS_COMMIT) {
                    uint64_t previous=0,has_position=0;
                    if(!vs_setting(q->user,key,&previous)) { ok=0;break; }
                    char presence[24];snprintf(presence,sizeof(presence),"position:%u",b);
                    if(!vs_setting(q->user,presence,&has_position)) { ok=0;break; }
                    if(!has_position)previous=window.since;
                    if(stamp<previous)stamp=previous;
                }
                ok=vs_put(q->user,key,stamp);
                snprintf(key,sizeof(key),"position:%u",b);
                if(ok)ok=vs_put(q->user,key,1);
            }
        }
        if(!ok || !feature_exec("RELEASE vs_pointer")) {
            feature_exec("ROLLBACK TO vs_pointer; RELEASE vs_pointer");r->feedback.failed=1;
        }
    } else r->feedback.failed=1;
}
