#ifndef MAIL_REPOSITORY_H
#define MAIL_REPOSITORY_H
/* Internal board-owner policy bridge, not supplied by UI callers. Remaining
 * persistence is the existing mailbox/mail_files API and worker-owned stores. */
int mail_repository_enabled(void);
#endif
