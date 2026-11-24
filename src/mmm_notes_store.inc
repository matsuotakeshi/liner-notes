/* Included by the board worker and the isolated SQLite tests. */
#include "mmm_notes_repository.h"
#include "catalog.h"

int mmm_notes_label_get(uint32_t board,char out[MMM_LABEL_MAX+1]) {
    *out=0;
    sqlite3_stmt *s=member_settings_statement("SELECT label FROM mmm_board_labels WHERE board=?");
    if(!s)return 0;
    int rc=sqlite3_bind_int64(s,1,board)==SQLITE_OK?sqlite3_step(s):SQLITE_ERROR;
    int ok=rc==SQLITE_DONE;
    if(rc==SQLITE_ROW && sqlite3_column_type(s,0)==SQLITE_TEXT) {
        const char *label=(const char *)sqlite3_column_text(s,0);
        int bytes=sqlite3_column_bytes(s,0);
        ok=label && bytes>0 && bytes<=MMM_LABEL_MAX && strlen(label)==(size_t)bytes;
        if(ok)memcpy(out,label,(size_t)bytes+1);
    }
    if(sqlite3_finalize(s)!=SQLITE_OK)ok=0;
    if(!ok)*out=0;
    return ok;
}

int mmm_notes_label_lookup(const char *label,uint32_t *board) {
    *board=0;
    sqlite3_stmt *s=member_settings_statement("SELECT board FROM mmm_board_labels WHERE label=?");
    if(!s)return 0;
    int rc=sqlite3_bind_text(s,1,label,-1,SQLITE_STATIC)==SQLITE_OK?sqlite3_step(s):SQLITE_ERROR;
    int ok=rc==SQLITE_DONE;
    if(rc==SQLITE_ROW && sqlite3_column_type(s,0)==SQLITE_INTEGER) {
        sqlite3_int64 id=sqlite3_column_int64(s,0);
        ok=id>0 && id<=CATALOG_MAX;
        if(ok)*board=(uint32_t)id;
    }
    if(sqlite3_finalize(s)!=SQLITE_OK)ok=0;
    if(!ok)*board=0;
    return ok;
}

int mmm_notes_label_save(uint32_t board,const char *label) {
    sqlite3_stmt *s=member_settings_statement(*label?
        "INSERT INTO mmm_board_labels(board,label) VALUES(?,?) ON CONFLICT(board) DO UPDATE SET label=excluded.label":
        "DELETE FROM mmm_board_labels WHERE board=?");
    if(!s)return 0;
    int ok=sqlite3_bind_int64(s,1,board)==SQLITE_OK;
    if(ok && *label)ok=sqlite3_bind_text(s,2,label,-1,SQLITE_STATIC)==SQLITE_OK;
    int rc=ok?sqlite3_step(s):SQLITE_ERROR;
    int duplicate=(rc&0xff)==SQLITE_CONSTRAINT && sqlite3_extended_errcode(features_db)==SQLITE_CONSTRAINT_UNIQUE;
    int finalized=sqlite3_finalize(s);
    return rc==SQLITE_DONE && finalized==SQLITE_OK?1:duplicate?-1:0;
}

int mmm_notes_scanlist_load(uint32_t user,uint32_t out[MMM_SCAN_MAX],unsigned *count) {
    *count=0;
    sqlite3_stmt *s=member_settings_statement("SELECT position,board FROM mmm_scanlist WHERE user=? ORDER BY position");
    if(!s)return 0;
    unsigned used=0;
    int ok=sqlite3_bind_int64(s,1,user)==SQLITE_OK,rc=SQLITE_ERROR;
    while(ok && (rc=sqlite3_step(s))==SQLITE_ROW) {
        sqlite3_int64 pos=sqlite3_column_int64(s,0),board=sqlite3_column_int64(s,1);
        ok=used<MMM_SCAN_MAX && pos==used+1 && board>0 && board<=CATALOG_MAX &&
            sqlite3_column_type(s,0)==SQLITE_INTEGER && sqlite3_column_type(s,1)==SQLITE_INTEGER;
        if(ok)out[used++]=(uint32_t)board;
    }
    if(sqlite3_finalize(s)!=SQLITE_OK)ok=0;
    if(ok && rc==SQLITE_DONE) { *count=used;return 1; }
    memset(out,0,MMM_SCAN_MAX*sizeof(*out));
    return 0;
}

int mmm_notes_scanlist_replace(uint32_t user,const uint32_t *boards,unsigned count) {
    if(count>MMM_SCAN_MAX || (!boards && count) || !member_settings_repository_ready() ||
       sqlite3_exec(features_db,"SAVEPOINT mmm_scanlist_replace",NULL,NULL,NULL)!=SQLITE_OK)return 0;
    sqlite3_stmt *s=member_settings_statement("DELETE FROM mmm_scanlist WHERE user=?");
    int ok=s && sqlite3_bind_int64(s,1,user)==SQLITE_OK && sqlite3_step(s)==SQLITE_DONE;
    if(sqlite3_finalize(s)!=SQLITE_OK)ok=0;
    if(ok && count) {
        s=member_settings_statement("INSERT INTO mmm_scanlist(user,position,board) VALUES(?,?,?)");
        ok=s!=NULL;
        for(unsigned i=0;ok && i<count;i++) {
            ok=sqlite3_bind_int64(s,1,user)==SQLITE_OK && sqlite3_bind_int(s,2,(int)i+1)==SQLITE_OK &&
                sqlite3_bind_int64(s,3,boards[i])==SQLITE_OK && sqlite3_step(s)==SQLITE_DONE &&
                sqlite3_reset(s)==SQLITE_OK;
        }
        if(sqlite3_finalize(s)!=SQLITE_OK)ok=0;
    }
    if(ok && sqlite3_exec(features_db,"RELEASE mmm_scanlist_replace",NULL,NULL,NULL)==SQLITE_OK)return 1;
    sqlite3_exec(features_db,"ROLLBACK TO mmm_scanlist_replace;RELEASE mmm_scanlist_replace",NULL,NULL,NULL);
    return 0;
}
