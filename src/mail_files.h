#ifndef MAIL_FILES_H
#define MAIL_FILES_H
#include <stdint.h>
#include <stddef.h>
#include "transfer.h"
/* Mail application adapter. Owned by the mail worker, separate from public vault. */
void mail_files_config(const char *);
void mail_files_close(void);
void mail_files_cleanup(void);
int mail_files_owned(uint32_t,uint32_t);
int mail_files_drop(uint32_t,uint32_t);
/* Private storage backend only. Core authenticates first; this layer derives
 * its own owner Principal and never accepts caller-supplied capabilities. */
VaultResult mail_files_execute(uint32_t,const TransferRequest *,TransferReply *);
/* Owned metadata, with no language/UI fields. List contains only the owner's
 * active or committed uploads; info also permits a live message participant. */
typedef struct { uint32_t id,size; int active; char name[128]; } MailFileInfo;
typedef struct { unsigned count; uint32_t next; MailFileInfo rows[8]; } MailFilePage;
int mail_files_info(uint32_t,uint32_t,MailFileInfo *);
int mail_files_page(uint32_t,uint32_t,MailFilePage *);
#endif
