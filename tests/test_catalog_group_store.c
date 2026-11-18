/* Real SQLite against the exact repository included in the board worker.
 * ACL/member journals are stubbed to isolate intent/transaction/replay order. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
static sqlite3 *features_db;
static int features_broken;
#include "catalog_group_store.inc"
static int acl_failure,member_failure,acl_done,member_done;
static unsigned acl_calls,member_calls,acl_changes,member_changes;
static void sql(const char *text) { assert(sqlite3_exec(features_db,text,NULL,NULL,NULL)==SQLITE_OK); }
static int scalar(const char *text) {
    sqlite3_stmt *s=NULL;assert(sqlite3_prepare_v2(features_db,text,-1,&s,NULL)==SQLITE_OK);
    assert(sqlite3_step(s)==SQLITE_ROW);int n=sqlite3_column_int(s,0);assert(sqlite3_finalize(s)==SQLITE_OK);return n;
}
int catalog_replace_group(uint32_t board,uint32_t actor,unsigned from,unsigned to) {
    assert(board==2 && actor==1 && from==10 && to==11);acl_calls++;
    assert(scalar("SELECT count(*) FROM group_moves WHERE board=2 AND actor=1 AND source=10 AND destination=11 AND done=0")==1);
    if(acl_failure)return 0;
    if(acl_done)return 2;
    acl_done=1;acl_changes++;return 1;
}
int members_group_move(uint32_t actor,unsigned from,unsigned to) {
    assert(actor==1 && from==10 && to==11 && acl_done);member_calls++;
    if(member_failure)return -1;
    if(!member_done) { member_changes++;member_done=1; }
    return 1;
}
static void reset(void) {
    if(features_db)assert(sqlite3_close(features_db)==SQLITE_OK);
    assert(sqlite3_open(":memory:",&features_db)==SQLITE_OK);features_broken=0;
    sql("CREATE TABLE delegated(board INTEGER,gid INTEGER,PRIMARY KEY(board,gid));CREATE TABLE group_moves(board INTEGER PRIMARY KEY,actor INTEGER,source INTEGER,destination INTEGER,done INTEGER);");
    acl_failure=member_failure=acl_done=member_done=0;acl_calls=member_calls=acl_changes=member_changes=0;
}
static void source(void) { sql("INSERT INTO delegated VALUES(2,10),(2,12),(3,10)"); }
static void complete(void) {
    assert(scalar("SELECT done FROM group_moves WHERE board=2")==1);
    assert(scalar("SELECT count(*) FROM delegated WHERE board=2 AND gid=10")==0);
    assert(scalar("SELECT count(*) FROM delegated WHERE board=2 AND gid=11")==1);
    assert(scalar("SELECT count(*) FROM delegated WHERE board=2 AND gid=12")==1);
    assert(scalar("SELECT count(*) FROM delegated WHERE board=3 AND gid=10")==1);
    assert(sqlite3_get_autocommit(features_db));
}
int main(void) {
    assert(!catalog_group_repository_ready());reset();
    assert(catalog_group_repository_delegate(1,2,10));
    assert(catalog_group_repository_delegate(1,2,10));
    assert(catalog_group_repository_delegate(1,2,11));
    assert(catalog_group_repository_delegate(1,3,10));
    assert(scalar("SELECT count(*) FROM delegated")==3);
    assert(catalog_group_repository_delegate(1,2,0));
    assert(catalog_group_repository_delegate(1,2,0));
    assert(scalar("SELECT count(*) FROM delegated")==1);
    assert(scalar("SELECT board FROM delegated")==3);
    puts("PASS: existing delegation schema, duplicate add, clear and board isolation");

    reset();source();assert(catalog_group_repository_move(1,2,10,11));complete();
    assert(acl_changes==1 && member_changes==1);
    assert(catalog_group_repository_recover());assert(catalog_group_repository_recover());
    assert(acl_calls==1 && member_calls==1);
    puts("PASS: intent before external journals; done plans skipped on repeated recovery");

    reset();source();sql("INSERT INTO delegated VALUES(2,11)");
    assert(catalog_group_repository_move(1,2,10,11));complete();
    assert(scalar("SELECT count(*) FROM delegated")==3);
    reset();source();sql("INSERT INTO group_moves VALUES(2,1,10,11,0)");
    assert(catalog_group_repository_recover());complete();
    puts("PASS: existing destination coalesces and startup recovers an untouched intent");

    reset();source();acl_failure=1;
    assert(!catalog_group_repository_move(1,2,10,11) && features_broken);
    assert(scalar("SELECT done FROM group_moves WHERE board=2")==0 && !member_calls);
    assert(!catalog_group_repository_delegate(1,2,12));
    assert(!catalog_group_repository_recover());
    features_broken=0;acl_failure=0;assert(catalog_group_repository_recover());complete();
    puts("PASS: ACL failure leaves intent pending and broken store refuses subsequent writes");

    reset();source();member_failure=1;
    assert(!catalog_group_repository_move(1,2,10,11) && features_broken && acl_done && !member_done);
    assert(scalar("SELECT done FROM group_moves WHERE board=2")==0);
    features_broken=0;member_failure=0;assert(catalog_group_repository_recover());complete();
    assert(acl_changes==1 && member_changes==1 && acl_calls==2);
    puts("PASS: restart after ACL-only progress reapplies idempotent work without losing the intent");

    reset();source();sql("CREATE TRIGGER fail_done BEFORE UPDATE OF done ON group_moves BEGIN SELECT RAISE(ABORT,'test-only');END;");
    assert(!catalog_group_repository_move(1,2,10,11) && features_broken);
    assert(acl_done && member_done && sqlite3_get_autocommit(features_db));
    assert(scalar("SELECT done FROM group_moves WHERE board=2")==0);
    assert(scalar("SELECT count(*) FROM delegated WHERE board=2 AND gid=10")==1);
    assert(scalar("SELECT count(*) FROM delegated WHERE board=2 AND gid=11")==0);
    sql("DROP TRIGGER fail_done");features_broken=0;
    assert(catalog_group_repository_recover());complete();
    assert(acl_changes==1 && member_changes==1);
    puts("PASS: completion failure rolls back delegation plus done marker, retaining external progress");

    reset();source();sql("PRAGMA query_only=ON");
    assert(!catalog_group_repository_delegate(1,2,11));
    assert(!catalog_group_repository_move(1,2,10,11));
    assert(!acl_calls && !member_calls && scalar("SELECT count(*) FROM group_moves")==0);
    assert(scalar("SELECT count(*) FROM delegated")==3);
    sql("PRAGMA query_only=OFF");sql("DROP TABLE delegated");
    assert(!catalog_group_repository_delegate(1,2,10));
    puts("PASS: read-only/schema failure does not publish an intent or call external writers");

    assert(sqlite3_close(features_db)==SQLITE_OK);features_db=NULL;
    assert(!catalog_group_repository_ready());
    assert(!catalog_group_repository_delegate(1,2,10));
    assert(!catalog_group_repository_move(1,2,10,11));
    assert(!catalog_group_repository_recover());
    puts("PASS: closed storage fails safely; all statements finalized");
    return 0;
}
