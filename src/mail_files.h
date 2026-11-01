#ifndef MAIL_FILES_H
#define MAIL_FILES_H
#include <stdint.h>
#include <stddef.h>
/* Mail application adapter. Owned by the mail worker, separate from public vault. */
void mail_files_config(const char *);
void mail_files_close(void);
void mail_files_cleanup(void);
int mail_files_owned(uint32_t,uint32_t);
int mail_files_drop(uint32_t,uint32_t);
void mail_files_transfer(uint32_t,const char *,char *,size_t);
void mail_files_list(uint32_t,uint32_t,int,char *,size_t,uint32_t *);
size_t mail_files_describe(uint32_t,uint32_t,int,char *,size_t);
#endif
