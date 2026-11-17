#ifndef MAIL_REPOSITORY_H
#define MAIL_REPOSITORY_H
#include <stdint.h>
/* Internal board-owner policy bridge, not supplied by UI callers. Remaining
 * persistence is the existing mailbox/mail_files API and worker-owned stores. */
int mail_repository_enabled(void);
/* Consume the borrowed body synchronously; write only this user's common
 * buffer. Do not retain it or call mailbox functions that invalidate caches. */
int mail_repository_buffer_save(uint32_t user,const char *body);
#endif
