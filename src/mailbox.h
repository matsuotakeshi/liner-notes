#ifndef MAILBOX_H
#define MAILBOX_H
#include <stdint.h>
#include "content_limits.h"
#include "mail_attachments.h"
typedef struct { uint32_t id,sender,recipient,parent,body_size; uint64_t time,body_offset; MailAttachments attachments; int read,deleted; char title[81]; } MailMessage;
uint32_t mailbox_attachment_last(void);
const char *mailbox_body(const MailMessage *);
int mailbox_attachment_used(uint32_t);
int mailbox_attachment_referenced(uint32_t);
int mailbox_attachment_readable(uint32_t,uint32_t);
int mailbox_open(const char *members_path);
void mailbox_close(void);
int mailbox_broken(void);
uint32_t mailbox_count(void);
uint32_t mailbox_search_next(uint32_t,const char *);
const MailMessage *mailbox_get(uint32_t id,uint32_t user);
const MailMessage *mailbox_draft(uint32_t user);
int mailbox_save_draft(uint32_t user,uint32_t to,uint32_t parent,const char *title,const char *body,const MailAttachments *attachments,uint32_t expected,uint32_t *revision);
int mailbox_delete_draft(uint32_t user,uint32_t expected);
int mailbox_send(uint32_t from,uint32_t to,uint32_t parent,const char *title,const char *body,const MailAttachments *attachments,uint32_t revision,uint32_t *id);
int mailbox_mark(uint32_t id,uint32_t user);
int mailbox_remove(uint32_t id,uint32_t user);
unsigned mailbox_unread(uint32_t user);
#endif
