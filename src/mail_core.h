#ifndef MAIL_CORE_H
#define MAIL_CORE_H
#include <stddef.h>
#include <stdint.h>
#include "mail_attachments.h"
#include "mail_files.h"

/* Internal single-worker API. Actor is supplied by the trusted session, never
 * by a client as an authorization flag. No retained pointers or shared outputs;
 * input/output memory must not overlap. No administrator override of ownership. */
typedef struct { uint32_t user,epoch; } MailActor;
typedef enum { MAIL_CORE_OK, MAIL_CORE_LOGIN_REQUIRED, MAIL_CORE_REVOKED,
    MAIL_CORE_MEMBER_STORAGE, MAIL_CORE_MODERATION_STORAGE, MAIL_CORE_DISABLED,
    MAIL_CORE_STORAGE, MAIL_CORE_UNAVAILABLE, MAIL_CORE_BODY_LIMIT, MAIL_CORE_BODY_STORAGE,
    MAIL_CORE_BUFFER_STORAGE } MailOutcome;
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
/* Worker-owned metadata, rechecking identity/feature on each entry. Listing is
 * owner-only, info is owner or a live message participant (never admin bypass).
 * Cursor is inclusive, unlike MailQuery. Active uploads keep the legacy '-' name. */
MailOutcome mail_core_files(const MailActor *,uint32_t cursor,MailFilePage *out);
MailOutcome mail_core_attachment_info(const MailActor *,uint32_t id,MailFileInfo *out);
/* Return message attachment IDs in stored order, without marking read or
 * inspecting blob availability. Transfer reauthorizes each file on execution. */
MailOutcome mail_core_attachment_batch(const MailActor *,uint32_t mail,MailAttachments *out);
/* Copy participant-visible body to the actor's persistent common buffer.
 * Does not mark read, alter mail/drafts or require attachment ownership. */
MailOutcome mail_core_import(const MailActor *,uint32_t mail);

/* Owned read models. Names are copied, sender=0 renders as guest. Read/list
 * authorization is participant-only, including for administrators. */
typedef struct {
    uint32_t id,sender,recipient,parent;
    uint64_t time;
    int read;
    char from[33],to[33],title[81];
} MailSummary;
typedef enum { MAIL_LIST_ALL, MAIL_LIST_SENT, MAIL_LIST_UNREAD, MAIL_LIST_INBOX,
               MAIL_LIST_SEARCH, MAIL_LIST_NEWS } MailListMode;
typedef struct {
    MailListMode mode;
    uint32_t cursor; /* exclusive; next is the last ID before the next match */
    const char *query; /* nonempty <=80 bytes for SEARCH; ignored otherwise */
    uint64_t since,until; /* NEWS: inclusive bounds; until=0 means no upper bound */
} MailQuery;
typedef struct { unsigned count,unread; uint32_t next; MailSummary rows[8]; } MailPage;
MailOutcome mail_core_list(const MailActor *,const MailQuery *,MailPage *out);
typedef enum { MAIL_READ_ID, MAIL_READ_UNREAD, MAIL_READ_LATEST, MAIL_READ_SEQUENCE } MailReadMode;
typedef struct {
    MailSummary message;
    MailAttachments attachments;
    uint32_t next;
    int sequence; /* success + sequence + message.id=0 means end, not denial */
} MailRead;
/* ID uses start; SEQUENCE uses inclusive start and direction. Descending starts
 * beyond the store clamp to its last ID; start=0 ends the sequence. Read marks
 * recipient mail before loading the body, preserving durable existing ordering.
 * A later body failure can therefore leave mail read, but exposes no payload.
 * List/search never marks mail. All failed outputs/body buffers are cleared. */
MailOutcome mail_core_read(const MailActor *,MailReadMode,uint32_t start,int descending,
                          MailRead *out,char *body,size_t capacity);
#endif
