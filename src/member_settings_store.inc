/* Included by the board-owned features store and isolated SQLite tests.
 * Keep the existing tables and keys; never turn a failed SELECT into a default. */
#include "member_settings_repository.h"
#include <time.h>
#include <sqlite3.h>
#include "ui_profile.h"
int member_settings_repository_ready(void) { return features_db && !features_broken; }
int member_settings_repository_now(uint64_t *stamp) {
    if(!stamp)return 0;
    *stamp=0;time_t now=time(NULL);if(now<0)return 0;
    *stamp=(uint64_t)now;return 1;
}
static sqlite3_stmt *member_settings_statement(const char *sql) {
    sqlite3_stmt *s=NULL;
    if(!member_settings_repository_ready() || sqlite3_prepare_v2(features_db,sql,-1,&s,NULL)!=SQLITE_OK) {
        sqlite3_finalize(s);return NULL;
    }
    return s;
}
int member_settings_repository_menu_get(uint32_t user,unsigned *mode) {
    if(!mode)return 0;
    *mode=0;if(!user)return 0;
    sqlite3_stmt *s=member_settings_statement("SELECT value FROM menu_preferences WHERE id=?");if(!s)return 0;
    int ok=sqlite3_bind_int64(s,1,user)==SQLITE_OK;unsigned value=1;
    int rc=ok?sqlite3_step(s):SQLITE_ERROR;
    if(rc==SQLITE_ROW) {
        sqlite3_int64 n=sqlite3_column_int64(s,0);
        ok=sqlite3_column_type(s,0)==SQLITE_INTEGER && n>=0 && n<=2;
        if(ok)value=(unsigned)n;
    } else ok=rc==SQLITE_DONE;
    if(sqlite3_finalize(s)!=SQLITE_OK)ok=0;
    if(ok)*mode=value;
    return ok;
}
int member_settings_repository_menu_save(uint32_t user,unsigned mode) {
    if(!user || mode>2)return 0;
    sqlite3_stmt *s=member_settings_statement("INSERT INTO menu_preferences VALUES(?,?) ON CONFLICT(id) DO UPDATE SET value=excluded.value");if(!s)return 0;
    int ok=sqlite3_bind_int64(s,1,user)==SQLITE_OK && sqlite3_bind_int(s,2,(int)mode)==SQLITE_OK && sqlite3_step(s)==SQLITE_DONE;
    if(sqlite3_finalize(s)!=SQLITE_OK)ok=0;
    return ok;
}
int member_settings_repository_ui_get(uint32_t user,char out[32]) {
    if(!out)return 0;
    *out=0;if(!user)return 0;
    sqlite3_stmt *s=member_settings_statement("SELECT profile FROM ui_preferences WHERE user=?");if(!s)return 0;
    int ok=sqlite3_bind_int64(s,1,user)==SQLITE_OK;
    int rc=ok?sqlite3_step(s):SQLITE_ERROR;
    if(rc==SQLITE_ROW) {
        const char *name=(const char *)sqlite3_column_text(s,0);
        ok=sqlite3_column_type(s,0)==SQLITE_TEXT && ui_profile_valid(name) && (int)strlen(name)==sqlite3_column_bytes(s,0);
        if(ok)strcpy(out,name);
    } else ok=rc==SQLITE_DONE;
    if(sqlite3_finalize(s)!=SQLITE_OK)ok=0;
    if(!ok)*out=0;
    return ok;
}
int member_settings_repository_ui_save(uint32_t user,const char *name) {
    if(!user || !name || (*name && !ui_profile_valid(name)))return 0;
    sqlite3_stmt *s=member_settings_statement(*name?"INSERT INTO ui_preferences VALUES(?,?) ON CONFLICT(user) DO UPDATE SET profile=excluded.profile":"DELETE FROM ui_preferences WHERE user=?");if(!s)return 0;
    int ok=sqlite3_bind_int64(s,1,user)==SQLITE_OK;
    if(ok && *name)ok=sqlite3_bind_text(s,2,name,-1,SQLITE_TRANSIENT)==SQLITE_OK;
    if(ok)ok=sqlite3_step(s)==SQLITE_DONE;
    if(sqlite3_finalize(s)!=SQLITE_OK)ok=0;
    return ok;
}
int member_settings_repository_check_get(uint32_t user,unsigned kind,const char *scope,uint64_t *stamp) {
    if(!stamp)return 0;
    *stamp=0;if(!user || kind>6 || !scope || (kind>=3 && kind<6 && *scope))return 0;
    sqlite3_stmt *s=member_settings_statement(*scope?"SELECT stamp FROM news_scoped WHERE user=? AND kind=? AND scope=?":"SELECT stamp FROM news WHERE user=? AND kind=?");if(!s)return 0;
    int ok=sqlite3_bind_int64(s,1,user)==SQLITE_OK && sqlite3_bind_int(s,2,(int)kind)==SQLITE_OK;
    if(ok && *scope)ok=sqlite3_bind_text(s,3,scope,-1,SQLITE_TRANSIENT)==SQLITE_OK;
    uint64_t value=0;int rc=ok?sqlite3_step(s):SQLITE_ERROR;
    if(rc==SQLITE_ROW) {
        sqlite3_int64 n=sqlite3_column_int64(s,0);
        ok=sqlite3_column_type(s,0)==SQLITE_INTEGER && n>=0;
        if(ok)value=(uint64_t)n;
    } else ok=rc==SQLITE_DONE;
    if(sqlite3_finalize(s)!=SQLITE_OK)ok=0;
    if(ok)*stamp=value;
    return ok;
}
int member_settings_repository_check_save(uint32_t user,unsigned kind,const char *scope,uint64_t stamp) {
    if(!user || kind>6 || !scope || (kind>=3 && kind<6 && *scope) || stamp>INT64_MAX)return 0;
    sqlite3_stmt *s=member_settings_statement(*scope?"INSERT INTO news_scoped(user,kind,stamp,scope) VALUES(?,?,?,?) ON CONFLICT(user,kind,scope) DO UPDATE SET stamp=max(stamp,excluded.stamp)":"INSERT INTO news VALUES(?,?,?) ON CONFLICT(user,kind) DO UPDATE SET stamp=max(stamp,excluded.stamp)");if(!s)return 0;
    int ok=sqlite3_bind_int64(s,1,user)==SQLITE_OK && sqlite3_bind_int(s,2,(int)kind)==SQLITE_OK && sqlite3_bind_int64(s,3,(sqlite3_int64)stamp)==SQLITE_OK;
    if(ok && *scope)ok=sqlite3_bind_text(s,4,scope,-1,SQLITE_TRANSIENT)==SQLITE_OK;
    if(ok)ok=sqlite3_step(s)==SQLITE_DONE;
    if(sqlite3_finalize(s)!=SQLITE_OK)ok=0;
    return ok;
}
int member_settings_repository_sequencer_set(uint32_t user,unsigned kind,uint64_t stamp) {
    if(!user || (kind<3 || kind>6) || stamp>INT64_MAX)return 0;
    sqlite3_stmt *s=member_settings_statement(
        "INSERT INTO news(user,kind,stamp) VALUES(?,?,?) ON CONFLICT(user,kind) DO UPDATE SET stamp=excluded.stamp");
    if(!s)return 0;
    int ok=sqlite3_bind_int64(s,1,user)==SQLITE_OK && sqlite3_bind_int(s,2,(int)kind)==SQLITE_OK &&
        sqlite3_bind_int64(s,3,(sqlite3_int64)stamp)==SQLITE_OK
        && sqlite3_step(s)==SQLITE_DONE;
    if(sqlite3_finalize(s)!=SQLITE_OK)ok=0;
    return ok;
}
