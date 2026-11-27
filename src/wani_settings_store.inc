/* Board worker owns these settings alongside the other profile preferences. */
#include "wani_core.h"
#include "catalog_core.h"
#include "wani_bbs_input.h"

#define WANI_SETTINGS_SCHEMA \
    "CREATE TABLE IF NOT EXISTS wani_board_views(board INTEGER PRIMARY KEY CHECK(board BETWEEN 0 AND 1024)," \
      "mode INTEGER NOT NULL CHECK(mode IN (1,2)));" \
    "CREATE TABLE IF NOT EXISTS wani_subscriptions(user INTEGER NOT NULL CHECK(user>0)," \
      "board INTEGER NOT NULL CHECK(board BETWEEN 1 AND 1024),PRIMARY KEY(user,board));"

static int wani_pattern(const char *patterns,const char *name,uint32_t id) {
    char number[16];snprintf(number,sizeof(number),"#%u",id);
    while(*patterns) {
        patterns+=strspn(patterns," ,\t");
        size_t length=strcspn(patterns," ,\t");
        if(!length || length>80)break;
        char pattern[81];
        memcpy(pattern,patterns,length);pattern[length]=0;
        if(wani_match(pattern,name) || (*pattern=='#' && !strcmp(pattern,number)))return 1;
        patterns+=length;
    }
    return 0;
}

static void wani_queries(const BoardRequest *q,BoardResult *r) {
    ArticleActor actor={q->user,q->user_epoch,q->board_grants};
    if(!q->user) { post_result(ARTICLE_LOGIN_REQUIRED,r);return; }
    if(q->action==WANI_BROWSE) {
        ArticlePage page;
        ArticleOutcome outcome;
        if(q->scope==0)outcome=article_browse_board(&actor,q->board,q->cursor,q->descending,&page);
        else if(q->scope==3)outcome=article_news_filtered(&actor,q->board,ARTICLE_HERE,q->cursor,q->since,q->until,q->notes_unread,&page);
        else if(q->scope==1 && q->notes_unread) {
            ArticleNotesPage notes;
            outcome=article_notes_window(&actor,q->board,q->cursor,q->since,q->until,&notes);
            if(outcome==ARTICLE_OK) {
                memset(&page,0,sizeof(page));page.next=notes.next;
                for(unsigned i=0;i<notes.count;i++) {
                    page.rows[page.count]=notes.rows[i].summary;
                    page.rows[page.count].created=notes.rows[i].created;
                    memcpy(page.rows[page.count++].author,notes.rows[i].author,sizeof(notes.rows[i].author));
                }
            }
        } else outcome=article_browse_notes(&actor,q->board,q->scope==2?q->number:0,q->cursor,
                                          q->descending,q->notes_unread,*q->title?q->title:NULL,&page);
        if(outcome!=ARTICLE_OK)post_result(outcome,r);
        else OPERATION_PAGE(r,ARTICLES,articles,page);
        return;
    }
    MmmScanList list={0};
    uint32_t prefix=0;
    for(uint32_t id=1;id<=CATALOG_MAX;id++) {
        if(!catalog_exists(id))continue;
        ArticleBoardEntry entry;
        ArticleOutcome outcome=article_enter_board(&actor,id,&entry);
        if(outcome==ARTICLE_BOARD_DENIED)continue;
        if(outcome!=ARTICLE_OK) { post_result(outcome,r);return; }
        if(q->action==WANI_RESOLVE) {
            if(q->number && catalog_parent(id)!=q->number)continue;
            if(wani_equal(entry.title,q->title)) { r->service_number=id;return; }
            const size_t n=strlen(q->title);
            if(!prefix && n>=2 && n<=strlen(entry.title)) {
                char candidate[81];
                memcpy(candidate,entry.title,n);candidate[n]=0;
                if(wani_equal(candidate,q->title))prefix=id;
            }
        } else if(!entry.menu && !entry.password_board && (!*q->title || wani_pattern(q->title,entry.title,id))) {
            if(list.count==MMM_SCAN_MAX) { post_result(ARTICLE_INVALID_QUERY,r);return; }
            MmmScanBoard *row=&list.rows[list.count++];
            row->board=id;memcpy(row->title,entry.title,sizeof(row->title));
        }
    }
    if(q->action==WANI_RESOLVE) {
        if(prefix)r->service_number=prefix;
        else post_result(ARTICLE_BOARD_DENIED,r);
    } else OPERATION_PAGE(r,MMM_SCANLIST,mmm_scanlist,list);
}

static void wani_configure(const BoardRequest *q,BoardResult *r) {
    if(q->action>=WANI_BROWSE && q->action<=WANI_SCAN_TARGETS) { wani_queries(q,r);return; }
    ArticleActor article={q->user,q->user_epoch,q->board_grants};
    if(q->action==WANI_WITHDRAW || q->action==WANI_RETITLE) {
        ArticleOutcome outcome=article_wani_change(&article,q->board,q->number,q->cursor==1,
                                                   q->action==WANI_RETITLE?q->title:NULL);
        if(outcome!=ARTICLE_OK)post_result(outcome,r);
        return;
    }
    CatalogActor manager={q->user,q->user_epoch};
    uint32_t board=q->number;
    ArticleBoardEntry entry;
    ArticleOutcome result=ARTICLE_OK;
    sqlite3_stmt *s=NULL;
    int ok=0;
    r->service_action=q->action;
    if(q->action==WANI_VIEW_GET || q->action==WANI_SUBSCRIBE || q->action==WANI_UNSUBSCRIBE) {
        result=article_enter_board(&article,board,&entry);
        if(result==ARTICLE_OK && (entry.password_board || entry.menu))result=ARTICLE_BOARD_DENIED;
        if(result!=ARTICLE_OK) { post_result(result,r);return; }
    }
    if(q->action==WANI_VIEW_SET) {
        CatalogOutcome permission=board?catalog_core_manage(&manager,board):catalog_core_admin(&manager);
        if(permission!=CATALOG_CORE_OK) {
            if(permission==CATALOG_CORE_REVOKED)r->revoked=1;
            r->feedback.failed=1;
            return;
        }
        if(q->scope<WANI_VIEW_DEFAULT || q->scope>WANI_VIEW_MNOTES) { r->feedback.failed=1;return; }
        const char *sql=q->scope?
            "INSERT INTO wani_board_views VALUES(?1,?2) ON CONFLICT(board) DO UPDATE SET mode=excluded.mode":
            "DELETE FROM wani_board_views WHERE board=?1";
        s=feature_stmt(sql);
        ok=s && sqlite3_bind_int64(s,1,board)==SQLITE_OK &&
           (!q->scope || sqlite3_bind_int(s,2,q->scope)==SQLITE_OK) && sqlite3_step(s)==SQLITE_DONE;
    } else if(q->action==WANI_VIEW_GET) {
        s=feature_stmt("SELECT mode FROM wani_board_views WHERE board=? OR board=0 ORDER BY board DESC LIMIT 1");
        int rc=s && sqlite3_bind_int64(s,1,board)==SQLITE_OK?sqlite3_step(s):SQLITE_ERROR;
        if(rc==SQLITE_ROW) {
            r->service_number=(uint32_t)sqlite3_column_int(s,0);
            ok=r->service_number==WANI_VIEW_FLAT || r->service_number==WANI_VIEW_MNOTES;
        } else if(rc==SQLITE_DONE) { r->service_number=WANI_VIEW_MNOTES;ok=1; }
    } else if(q->action==WANI_SUBSCRIPTIONS) {
        if(!q->user || members_pending(q->user) || moderation_suspended(q->user) ||
           moderation_epoch(q->user)!=q->user_epoch) { r->feedback.failed=1;return; }
        MmmScanList list={.custom=1};
        s=feature_stmt("SELECT board FROM wani_subscriptions WHERE user=? ORDER BY board");
        int rc=s && sqlite3_bind_int64(s,1,q->user)==SQLITE_OK?SQLITE_OK:SQLITE_ERROR;
        if(rc==SQLITE_OK)while((rc=sqlite3_step(s))==SQLITE_ROW) {
            uint32_t id=(uint32_t)sqlite3_column_int64(s,0);
            result=article_enter_board(&article,id,&entry);
            if(result==ARTICLE_BOARD_DENIED || (result==ARTICLE_OK && (entry.password_board || entry.menu)))continue;
            if(result!=ARTICLE_OK || list.count==MMM_SCAN_MAX) { rc=SQLITE_ERROR;break; }
            MmmScanBoard *row=&list.rows[list.count++];
            row->board=id;memcpy(row->title,entry.title,sizeof(row->title));
        }
        ok=rc==SQLITE_DONE;
        if(ok)OPERATION_PAGE(r,MMM_SCANLIST,mmm_scanlist,list);
    } else if(q->action==WANI_SUBSCRIBE || q->action==WANI_UNSUBSCRIBE) {
        if(!q->user) { r->feedback.failed=1;return; }
        const char *sql=q->action==WANI_SUBSCRIBE?
            "INSERT INTO wani_subscriptions(user,board) SELECT ?1,?2 "
            "WHERE (SELECT count(*) FROM wani_subscriptions WHERE user=?1)<64 ON CONFLICT DO NOTHING":
            "DELETE FROM wani_subscriptions WHERE user=?1 AND board=?2";
        s=feature_stmt(sql);
        ok=s && sqlite3_bind_int64(s,1,q->user)==SQLITE_OK && sqlite3_bind_int64(s,2,board)==SQLITE_OK &&
           sqlite3_step(s)==SQLITE_DONE;
        if(ok && q->action==WANI_SUBSCRIBE && !sqlite3_changes(features_db)) {
            sqlite3_finalize(s);
            s=feature_stmt("SELECT 1 FROM wani_subscriptions WHERE user=? AND board=?");
            ok=s && sqlite3_bind_int64(s,1,q->user)==SQLITE_OK &&
                sqlite3_bind_int64(s,2,board)==SQLITE_OK && sqlite3_step(s)==SQLITE_ROW;
        }
    } else if(q->action>=WANI_SEQUENCE_GET && q->action<=WANI_SEQUENCE_ADVANCE) {
        MemberActor member={q->user,q->user_epoch};
        MemberCheckWindow window;
        MemberOutcome outcome;
        if(q->action==WANI_SEQUENCE_GET) {
            outcome=member_core_check_begin(&member,MEMBER_CHECK_WANI,"",&window);
            if(outcome==MEMBER_CORE_OK) { r->since=window.since;r->until=window.until; }
        } else if(q->action==WANI_SEQUENCE_SET)
            outcome=member_core_sequencer_set(&member,MEMBER_CHECK_WANI,q->until);
        else outcome=member_core_check_commit(&member,MEMBER_CHECK_WANI,"",q->until);
        if(outcome!=MEMBER_CORE_OK)member_result(outcome,r);
        return;
    }
    if(sqlite3_finalize(s)!=SQLITE_OK || !ok)r->feedback.failed=1;
}
