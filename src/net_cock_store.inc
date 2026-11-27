/* Accessed only on the board worker. */
#include "net_cock_core.h"

#define NET_COCK_SCHEMA \
    "CREATE TABLE IF NOT EXISTS net_cock_mode(user INTEGER PRIMARY KEY CHECK(user>0)," \
        "mode INTEGER NOT NULL CHECK(mode IN (0,1)));" \
    "CREATE TABLE IF NOT EXISTS net_cock_subscriptions(user INTEGER NOT NULL CHECK(user>0)," \
        "board INTEGER NOT NULL CHECK(board BETWEEN 1 AND 1024),PRIMARY KEY(user,board));"

static void net_cock_operation(const BoardRequest *q,BoardResult *r) {
    ArticleActor actor={q->user,q->user_epoch,q->board_grants};
    ArticleOutcome outcome=ARTICLE_OK;
    r->service_action=q->action;
    if(q->action==NET_HEADER) {
        ArticleView view;
        outcome=article_header(&actor,q->board,q->number,&view);
        if(outcome==ARTICLE_OK && view.state==ARTICLE_VISIBLE) {
            OperationContent *content=&r->content;
            content->kind=1;content->state=view.state;content->id=view.id;content->parent=view.parent;
            content->board=view.board;content->root=view.root;content->at=view.created;content->size=view.body_size;
            strcpy(content->author,view.author);strcpy(content->title,view.title);
        } else if(outcome==ARTICLE_OK)outcome=ARTICLE_NOT_FOUND;
        if(outcome!=ARTICLE_OK)post_result(outcome,r);
        return;
    }
    if(q->action==NET_LOOKUP) {
        outcome=article_board_named_folded(&actor,q->title,&r->service_number);
        if(outcome!=ARTICLE_OK)post_result(outcome,r);
        return;
    }
    if(q->action==NET_WITHDRAW) {
        outcome=article_net_withdraw(&actor,q->board,q->number,q->cursor==1);
        if(outcome!=ARTICLE_OK)post_result(outcome,r);
        return;
    }
    MemberActor own={q->user,q->user_epoch};
    MemberCheckWindow window;
    MemberOutcome member=q->action==NET_MOVE?member_core_sequencer_set(&own,MEMBER_CHECK_NET_COCK,q->until):
        member_core_check_begin(&own,MEMBER_CHECK_NET_COCK,"",&window);
    if(member!=MEMBER_CORE_OK) { member_result(member,r);return; }
    if(q->action==NET_MOVE)return;
    if(q->action==NET_SCAN_COMMIT) {
        member=member_core_check_commit(&own,MEMBER_CHECK_NET_COCK,"",q->until);
        if(member!=MEMBER_CORE_OK)member_result(member,r);
        return;
    }
    if(q->action==NET_SCAN_WINDOW) { r->since=window.since;r->until=window.until; }
    sqlite3_stmt *s=NULL;
    int ok=0;
    if(q->action==NET_MODE) {
        s=feature_stmt("SELECT mode FROM net_cock_mode WHERE user=?");
        int rc=s && sqlite3_bind_int64(s,1,q->user)==SQLITE_OK?sqlite3_step(s):SQLITE_ERROR;
        if(rc==SQLITE_ROW)r->service_number=(uint32_t)sqlite3_column_int(s,0);
        ok=rc==SQLITE_ROW || rc==SQLITE_DONE;
    } else if(q->action==NET_SET_MODE && q->number<=1) {
        s=feature_stmt("INSERT INTO net_cock_mode VALUES(?,?) ON CONFLICT(user) DO UPDATE SET mode=excluded.mode");
        ok=s && sqlite3_bind_int64(s,1,q->user)==SQLITE_OK && sqlite3_bind_int64(s,2,q->number)==SQLITE_OK &&
            sqlite3_step(s)==SQLITE_DONE;
        if(ok)r->service_number=q->number;
    } else if(q->action==NET_SUBSCRIBE && q->number && (q->scope==0 || q->scope==1)) {
        ArticleBoardEntry entry;
        outcome=article_enter_board(&actor,q->number,&entry);
        if(outcome!=ARTICLE_OK || entry.password_board || entry.menu) {
            post_result(outcome==ARTICLE_OK?ARTICLE_BOARD_DENIED:outcome,r);return;
        }
        s=feature_stmt(q->scope?
            "INSERT INTO net_cock_subscriptions SELECT ?1,?2 WHERE"
            " (SELECT count(*) FROM net_cock_subscriptions WHERE user=?1)<64 OR"
            " EXISTS(SELECT 1 FROM net_cock_subscriptions WHERE user=?1 AND board=?2) ON CONFLICT DO NOTHING":
            "DELETE FROM net_cock_subscriptions WHERE user=? AND board=?");
        ok=s && sqlite3_bind_int64(s,1,q->user)==SQLITE_OK && sqlite3_bind_int64(s,2,q->number)==SQLITE_OK &&
            sqlite3_step(s)==SQLITE_DONE;
        if(ok && q->scope && !sqlite3_changes(features_db)) {
            sqlite3_finalize(s);
            s=feature_stmt("SELECT 1 FROM net_cock_subscriptions WHERE user=? AND board=?");
            ok=s && sqlite3_bind_int64(s,1,q->user)==SQLITE_OK && sqlite3_bind_int64(s,2,q->number)==SQLITE_OK &&
                sqlite3_step(s)==SQLITE_ROW;
        }
    } else if(q->action==NET_SUBSCRIPTION_NEXT) {
        uint32_t board=q->number;
        ArticleBoardEntry entry;
        if(board>CATALOG_MAX) { r->feedback.failed=1;return; }
        for(board++;board<=CATALOG_MAX;board++) {
            if(!catalog_exists(board))continue;
            outcome=article_enter_board(&actor,board,&entry);
            if(outcome==ARTICLE_BOARD_DENIED || (outcome==ARTICLE_OK && (entry.password_board || entry.menu)))continue;
            if(outcome!=ARTICLE_OK) { post_result(outcome,r);return; }
            break;
        }
        if(board>CATALOG_MAX)return;
        s=feature_stmt("SELECT 1 FROM net_cock_subscriptions WHERE user=? AND board=?");
        int rc=s && sqlite3_bind_int64(s,1,q->user)==SQLITE_OK && sqlite3_bind_int64(s,2,board)==SQLITE_OK?
            sqlite3_step(s):SQLITE_ERROR;
        ok=rc==SQLITE_DONE || rc==SQLITE_ROW;
        if(ok) { r->service_number=board;r->service_next=rc==SQLITE_ROW;strcpy(r->service_text,entry.title); }
    } else if(q->action==NET_SCANLIST || q->action==NET_SCAN_WINDOW) {
        MmmScanList list={.custom=1};
        s=feature_stmt("SELECT board FROM net_cock_subscriptions WHERE user=? ORDER BY board");
        int rc=s && sqlite3_bind_int64(s,1,q->user)==SQLITE_OK?SQLITE_OK:SQLITE_ERROR;
        if(rc==SQLITE_OK)while((rc=sqlite3_step(s))==SQLITE_ROW) {
            uint32_t board=(uint32_t)sqlite3_column_int64(s,0);
            ArticleBoardEntry entry;
            outcome=article_enter_board(&actor,board,&entry);
            if(outcome==ARTICLE_BOARD_DENIED || (outcome==ARTICLE_OK && (entry.password_board || entry.menu)))continue;
            if(outcome!=ARTICLE_OK || list.count==MMM_SCAN_MAX) { rc=SQLITE_ERROR;break; }
            MmmScanBoard *row=&list.rows[list.count++];
            row->board=board;strcpy(row->title,entry.title);
        }
        ok=rc==SQLITE_DONE;
        if(ok)OPERATION_PAGE(r,MMM_SCANLIST,mmm_scanlist,list);
    }
    if(sqlite3_finalize(s)!=SQLITE_OK || !ok)r->feedback.failed=1;
}
