#ifndef MAIL_NAVIGATION_CORE_H
#define MAIL_NAVIGATION_CORE_H
#include <stdint.h>
#include "mail_query.h"
#include "traversal_core.h"

/* Per-session values, owned by one event loop. No UI, I/O, persistence or ACL
 * grants. Worker Mail Core must authorize every resulting operation. */
typedef struct {
    uint32_t current,cursor,delete_id;
    int reading;
    MailListMode filter;
    char query[81];
} MailNavigation;
typedef struct { MailListMode filter;uint32_t cursor;char query[81]; } MailListing;

/* Failed selection leaves state intact. Adapter validates NEWS date syntax.
 * Plain lists retain the old query internally, but never expose it in a plan.
 * Query may point into state; returned plans contain no borrowed pointers. */
int mail_navigation_filter(MailNavigation *,MailListMode,const char *query,uint32_t cursor);
MailListing mail_navigation_listing(const MailNavigation *);
/* Clears only listing state on authentication/logout, as before. */
void mail_navigation_clear_filter(MailNavigation *);
void mail_navigation_confirm(MailNavigation *,uint32_t id);
/* Consumes confirmation on either answer, even if subsequent queueing fails. */
uint32_t mail_navigation_confirmed(MailNavigation *,int yes);

typedef enum {
    MAIL_EVENT_OTHER,MAIL_EVENT_LIST,MAIL_EVENT_FILES,MAIL_EVENT_READ,
    MAIL_EVENT_BEGIN,MAIL_EVENT_REPLY,MAIL_EVENT_RESUME,MAIL_EVENT_SAVE,
    MAIL_EVENT_SEND,MAIL_EVENT_ATTACH
} MailSessionEvent;
/* Observe only after completion identity/slot and cancellation checks.
 * Failure (id=0) retains selection. Deletion does not synthesize a new selection.
 * Page cursor is applied separately, only by the awaited mail operation. */
void mail_navigation_observe(MailNavigation *,MailSessionEvent,uint32_t id);
void mail_navigation_page(MailNavigation *,MailSessionEvent,uint32_t next);
ReaderPosition mail_navigation_start(int descending);
ReaderPosition mail_navigation_move(const MailNavigation *,ReaderMove,int descending,uint32_t distance);

/* Editor owns actual text/attachments and secure erasure. This value describes
 * which existing lifecycle transition is warranted by a validated reply. */
typedef enum { MAIL_DRAFT_KEEP,MAIL_DRAFT_OPEN,MAIL_DRAFT_SAVED,
               MAIL_DRAFT_SENT,MAIL_DRAFT_RETRY,MAIL_DRAFT_ATTACH } MailDraftEffect;
MailDraftEffect mail_draft_effect(MailSessionEvent,uint32_t recipient,uint32_t id,uint32_t revision);
#endif
