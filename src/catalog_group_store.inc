/* Included by the single-owner board store and the exact SQLite unit test.
 * Persistent intents permit replay across the existing separate journals. */
#include "catalog_group_repository.h"
#include "catalog.h"
#include "members.h"
#include <sqlite3.h>
int catalog_group_repository_ready(void) { return features_db && !features_broken; }
static int group_exec(const char *sql) { return sqlite3_exec(features_db,sql,NULL,NULL,NULL)==SQLITE_OK; }
static sqlite3_stmt *group_statement(const char *sql) {
    sqlite3_stmt *s=NULL;if(sqlite3_prepare_v2(features_db,sql,-1,&s,NULL)!=SQLITE_OK)return NULL;return s;
}
int catalog_group_repository_recover(void) {
    if(!catalog_group_repository_ready())return 0;
    for(;;) {
        sqlite3_stmt *s=group_statement("SELECT board,actor,source,destination FROM group_moves WHERE done=0 LIMIT 1");if(!s)return 0;
        int rc=sqlite3_step(s);if(rc==SQLITE_DONE) { sqlite3_finalize(s);return 1; }if(rc!=SQLITE_ROW) { sqlite3_finalize(s);return 0; }
        uint32_t board=(uint32_t)sqlite3_column_int(s,0),actor=(uint32_t)sqlite3_column_int(s,1);unsigned from=(unsigned)sqlite3_column_int(s,2),to=(unsigned)sqlite3_column_int(s,3);sqlite3_finalize(s);
        if(catalog_replace_group(board,actor,from,to)<=0 || members_group_move(actor,from,to)<=0)return 0;
        if(!group_exec("BEGIN IMMEDIATE"))return 0;
        s=group_statement("INSERT OR IGNORE INTO delegated SELECT board,? FROM delegated WHERE board=? AND gid=?");if(!s)goto rollback;
        sqlite3_bind_int(s,1,(int)to);sqlite3_bind_int(s,2,(int)board);sqlite3_bind_int(s,3,(int)from);rc=sqlite3_step(s);sqlite3_finalize(s);if(rc!=SQLITE_DONE)goto rollback;
        s=group_statement("DELETE FROM delegated WHERE board=? AND gid=?");if(!s)goto rollback;
        sqlite3_bind_int(s,1,(int)board);sqlite3_bind_int(s,2,(int)from);rc=sqlite3_step(s);sqlite3_finalize(s);if(rc!=SQLITE_DONE)goto rollback;
        s=group_statement("UPDATE group_moves SET done=1 WHERE board=?");if(!s)goto rollback;
        sqlite3_bind_int(s,1,(int)board);rc=sqlite3_step(s);sqlite3_finalize(s);if(rc!=SQLITE_DONE || !group_exec("COMMIT"))goto rollback;
        continue;
rollback:group_exec("ROLLBACK");return 0;
    }
}
int catalog_group_repository_delegate(uint32_t actor,uint32_t board,unsigned group) {
    (void)actor;
    if(!catalog_group_repository_ready())return 0;
    sqlite3_stmt *s=group_statement(group?"INSERT OR IGNORE INTO delegated VALUES(?,?)":"DELETE FROM delegated WHERE board=?");
    if(!s)return 0;
    sqlite3_bind_int(s,1,(int)board);if(group)sqlite3_bind_int(s,2,(int)group);
    int ok=sqlite3_step(s)==SQLITE_DONE;sqlite3_finalize(s);return ok;
}
int catalog_group_repository_move(uint32_t actor,uint32_t board,unsigned from,unsigned to) {
    if(!catalog_group_repository_ready())return 0;
    sqlite3_stmt *s=group_statement("INSERT INTO group_moves VALUES(?,?,?,?,0) ON CONFLICT(board) DO UPDATE SET actor=excluded.actor,source=excluded.source,destination=excluded.destination,done=0");
    if(!s)return 0;
    sqlite3_bind_int(s,1,(int)board);sqlite3_bind_int(s,2,(int)actor);
    sqlite3_bind_int(s,3,(int)from);sqlite3_bind_int(s,4,(int)to);
    int ok=sqlite3_step(s)==SQLITE_DONE;sqlite3_finalize(s);if(!ok)return 0;
    if(!catalog_group_repository_recover()) { features_broken=1;return 0; }
    return 1;
}
