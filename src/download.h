#ifndef DOWNLOAD_H
#define DOWNLOAD_H
#include "vault.h"
/* Public-vault receipts. No account database or private-mail access. */
uint64_t downloads_count(uint32_t);
int downloads_open(const char *directory);
void downloads_close(void);
int downloads_prune(Vault *);
void downloads_guard(int (*guard)(void *),void *context);
int downloads_command(Vault *,const Principal *,const char *,const char *,char *,size_t);
void downloads_report(Vault *,const Principal *,int history,uint32_t file,uint32_t cursor,char *,size_t);
#endif
