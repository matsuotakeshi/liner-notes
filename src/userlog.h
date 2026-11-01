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
void userlog_table(uint32_t,uint32_t,int,uint64_t,char *,size_t);
void userlog_report(uint32_t user,uint32_t cursor,int all,int stats,uint64_t now,char *out,size_t size);
#endif
