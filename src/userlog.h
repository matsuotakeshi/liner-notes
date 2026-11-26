#ifndef USERLOG_H
#define USERLOG_H
#include <stdint.h>
#include <stddef.h>
/* Runtime-owned connection ledger; no passwords, IP addresses or member DB. */
int userlog_open(const char *base);
int userlog_open_file(const char *path);
void userlog_close(void);
int userlog_connect(int slot,const char *transport,uint64_t now);
int userlog_update(int slot,uint32_t user,uint64_t now);
int userlog_disconnect(int slot,uint64_t now);
extern unsigned userlog_keep;
extern int userlog_public;
int userlog_maintain(void);
typedef struct {
    uint32_t id,connection,user;
    char kind;
    uint64_t at,seconds;
} UserlogRow;
typedef struct {
    uint32_t user,next;
    unsigned count;
    UserlogRow rows[8];
    uint64_t logins,seconds,last;
} UserlogReport;
/* Runtime only, after Activity Core authorizes the requested account/scope. */
const char *userlog_snapshot(uint32_t user,uint32_t cursor,int all,int stats,uint64_t now,UserlogReport *);
typedef struct {
    uint32_t connection,user;
    uint64_t login_at,logout_at,seconds,ordinal;
    char transport[8];
} UserlogSessionRow;
typedef struct { unsigned count; uint32_t next; UserlogSessionRow rows[8]; } UserlogSessionPage;
const char *userlog_session_page(uint32_t user,uint32_t cursor,int all,uint64_t now,UserlogSessionPage *);
void userlog_table(uint32_t,uint32_t,int,uint64_t,char *,size_t);
void userlog_report(uint32_t user,uint32_t cursor,int all,int stats,uint64_t now,char *out,size_t size);
#endif
