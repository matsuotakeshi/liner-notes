#ifndef MAIL_CORE_H
#define MAIL_CORE_H
#include <stddef.h>
#include <stdint.h>
#include "mail_attachments.h"

/* Internal single-worker API. Actor is supplied by the trusted session, never
 * by a client as an authorization flag. No retained pointers or shared outputs;
 * input/output memory must not overlap. No administrator override of ownership. */
typedef struct { uint32_t user,epoch; } MailActor;
typedef enum { MAIL_CORE_OK, MAIL_CORE_LOGIN_REQUIRED, MAIL_CORE_REVOKED,
    MAIL_CORE_MEMBER_STORAGE, MAIL_CORE_MODERATION_STORAGE, MAIL_CORE_DISABLED,
    MAIL_CORE_STORAGE, MAIL_CORE_UNAVAILABLE, MAIL_CORE_BODY_LIMIT } MailOutcome;
typedef struct { uint32_t to,parent; char title[81]; } MailCompose;
typedef struct {
    uint32_t revision,to,parent;
    char recipient[33],title[81];
    MailAttachments attachments;
} MailDraft;
typedef struct {
    uint32_t to,parent,revision;
    const char *title,*body;
    MailAttachments attachments;
} MailWrite;
/* reply=0 resolves login; otherwise reply must be an incoming mail owned by
 * actor. Empty title inherits the reply subject. Does not mark mail read. */
MailOutcome mail_core_prepare(const MailActor *,const char *login,uint32_t reply,
                              const char *title,MailCompose *out);
/* Metadata remains available for cleaning up an unusable draft. Resume also
 * rechecks recipient and reply eligibility and copies body to caller storage.
 * Body is not loaded for metadata-only requests. All outputs clear on failure. */
MailOutcome mail_core_draft_get(const MailActor *,int resume,MailDraft *out,
                                char *body,size_t capacity);
MailOutcome mail_core_draft_save(const MailActor *,const MailWrite *,uint32_t *revision);
MailOutcome mail_core_draft_delete(const MailActor *,uint32_t revision);
MailOutcome mail_core_send(const MailActor *,const MailWrite *,uint32_t *id);
MailOutcome mail_core_delete(const MailActor *,uint32_t id);
/* Selection is ephemeral. Add validates ownership, detach/clear may remove
 * stale selections. Save/send revalidate every selected attachment. */
MailOutcome mail_core_attachment_select(const MailActor *,const MailAttachments *,
                                        uint32_t id,int detach,MailAttachments *out);
MailOutcome mail_core_attachment_drop(const MailActor *,uint32_t id);
#endif
