/* Real SQLite checks for the exact implementation included in the board store. */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <sqlite3.h>
static sqlite3 *features_db;
static int features_broken;
#include "member_settings_store.inc"
static void sql(const char *text) { assert(sqlite3_exec(features_db,text,NULL,NULL,NULL)==SQLITE_OK); }
int main(void) {
    assert(sqlite3_open(":memory:",&features_db)==SQLITE_OK);
    sql("CREATE TABLE menu_preferences(id INTEGER PRIMARY KEY,value INTEGER);CREATE TABLE news(user INTEGER,kind INTEGER,stamp INTEGER NOT NULL,PRIMARY KEY(user,kind));CREATE TABLE news_scoped(user INTEGER,kind INTEGER,scope TEXT,stamp INTEGER NOT NULL,PRIMARY KEY(user,kind,scope));");
    sql(UI_PREFERENCE_SCHEMA);
    char profile[32];assert(member_settings_repository_ui_get(1,profile) && !*profile);
    assert(member_settings_repository_ui_save(1,"future-ui"));
    assert(member_settings_repository_ui_get(1,profile) && !strcmp(profile,"future-ui"));
    sql("BEGIN");assert(member_settings_repository_ui_save(1,"rtbbs"));sql("ROLLBACK");
    assert(member_settings_repository_ui_get(1,profile) && !strcmp(profile,"future-ui"));
    assert(!member_settings_repository_ui_save(1,"INVALID UI"));
    assert(!member_settings_repository_ui_save(0,"rtbbs"));
    assert(member_settings_repository_ui_save(1,""));
    assert(member_settings_repository_ui_get(1,profile) && !*profile);
    unsigned mode=99;uint64_t stamp=99;
    assert(member_settings_repository_menu_get(1,&mode) && mode==1);
    assert(member_settings_repository_check_get(1,0,"",&stamp) && !stamp);
    assert(member_settings_repository_check_get(1,0,"2:0",&stamp) && !stamp);
    for(unsigned i=0;i<3;i++) {
        assert(member_settings_repository_menu_save(1,i));assert(member_settings_repository_menu_get(1,&mode) && mode==i);
        assert(member_settings_repository_check_save(1,i,"",100+i));
    }
    assert(member_settings_repository_menu_get(2,&mode) && mode==1);
    assert(member_settings_repository_check_save(1,0,"2:0",200));
    assert(member_settings_repository_check_save(1,0,"2:2",300));
    assert(member_settings_repository_check_save(1,2,"quote'path",400));
    assert(member_settings_repository_check_get(1,2,"quote'path",&stamp) && stamp==400);
    assert(member_settings_repository_check_get(1,0,"2:0",&stamp) && stamp==200);
    assert(member_settings_repository_check_get(1,0,"2:2",&stamp) && stamp==300);
    assert(member_settings_repository_check_get(1,0,"",&stamp) && stamp==100);
    assert(member_settings_repository_check_get(2,0,"2:0",&stamp) && !stamp);
    assert(member_settings_repository_check_get(1,1,"",&stamp) && stamp==101);
    puts("PASS: existing schema/defaults, menu modes, per-user/kind/scope isolation and bound SQL text");
    assert(member_settings_repository_check_save(1,0,"2:0",199));
    assert(member_settings_repository_check_get(1,0,"2:0",&stamp) && stamp==200);
    assert(member_settings_repository_check_save(1,0,"2:0",200));
    assert(member_settings_repository_check_get(1,0,"2:0",&stamp) && stamp==200);
    assert(member_settings_repository_check_save(1,0,"",INT64_MAX));
    assert(member_settings_repository_check_get(1,0,"",&stamp) && stamp==INT64_MAX);
    assert(!member_settings_repository_check_save(1,0,"",UINT64_MAX));
    sql("BEGIN");assert(member_settings_repository_menu_save(1,0));assert(member_settings_repository_check_save(1,0,"2:0",500));sql("ROLLBACK");
    assert(member_settings_repository_menu_get(1,&mode) && mode==2);
    assert(member_settings_repository_check_get(1,0,"2:0",&stamp) && stamp==200);
    puts("PASS: monotonic/idempotent commits, SQLite integer bounds and rollback preservation");
    sql("PRAGMA query_only=ON");
    assert(!member_settings_repository_menu_save(1,0));assert(!member_settings_repository_check_save(1,0,"2:0",600));
    assert(member_settings_repository_menu_get(1,&mode) && mode==2);
    assert(member_settings_repository_check_get(1,0,"2:0",&stamp) && stamp==200);
    sql("PRAGMA query_only=OFF");
    const char *bad[]={"-1","'broken'","1.5","NULL"};
    for(unsigned i=0;i<4;i++) {
        char command[256];snprintf(command,sizeof(command),"UPDATE menu_preferences SET value=%s",bad[i]);sql(command);
        mode=99;assert(!member_settings_repository_menu_get(1,&mode) && !mode);
        if(i<3) {
            snprintf(command,sizeof(command),"UPDATE news_scoped SET stamp=%s",bad[i]);sql(command);
            stamp=99;assert(!member_settings_repository_check_get(1,0,"2:0",&stamp) && !stamp);
        }
    }
    sql("DROP TABLE news;DROP TABLE menu_preferences");
    stamp=99;assert(!member_settings_repository_check_get(1,0,"",&stamp) && !stamp);
    mode=99;assert(!member_settings_repository_menu_get(1,&mode) && !mode);
    features_broken=1;assert(!member_settings_repository_ready());
    assert(!member_settings_repository_check_get(1,0,"2:0",&stamp) && !stamp);
    assert(sqlite3_next_stmt(features_db,NULL)==NULL);
    assert(sqlite3_close(features_db)==SQLITE_OK);features_db=NULL;
    assert(!member_settings_repository_ready());
    puts("PASS: read-only writes rejected; malformed/missing/failed reads never become defaults; statements finalized");
    return 0;
}
