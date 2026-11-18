#ifndef LINER_NOTIFICATION_CORE_H
#define LINER_NOTIFICATION_CORE_H
#include <stdint.h>
/* Trusted session snapshots; no caller-supplied credentials or message content.
 * The event loop owns each state. Core does not authenticate or render output. */
enum {
    NOTICE_DOOR=1u<<0, NOTICE_BINARY=1u<<1, NOTICE_TRANSFER=1u<<2,
    NOTICE_SECRET=1u<<3, NOTICE_PENDING=1u<<4, NOTICE_PAGE=1u<<5,
    NOTICE_EDITOR=1u<<6, NOTICE_TIMER=1u<<7, NOTICE_INPUT=1u<<8,
    NOTICE_CHAT=1u<<9, NOTICE_SCAN=1u<<10
};
typedef struct {
    uint32_t user;unsigned blocked;
    int active,closing,interactive,restore_interactive;
} NotificationAudience;
typedef struct {
    int denied;uint32_t revision,arrivals;
} NotificationMail;
typedef struct {
    NotificationMail mail;
    char announcements[4096];
} NotificationState;
/* Login replaces account preference state; logout/revoke discard arrivals only. */
void notification_mail_reset(NotificationState *);
void notification_mail_discard(NotificationState *);
void notification_mail_preference(NotificationState *,uint32_t current_user,uint32_t user,int deny,uint32_t revision);
void notification_mail_arrived(NotificationState *,const NotificationAudience *,uint32_t recipient);
/* Consumes an eligible aggregate exactly once; returns 0 while delivery is blocked. */
uint32_t notification_mail_take(NotificationState *,const NotificationAudience *);
/* Announcement admission is for an already authorized administrator notice.
 * Bounded append is atomic. Peek borrows until append/clear/disposal. */
int notification_announce(NotificationState *,const NotificationAudience *,const char *);
const char *notification_announcement_peek(const NotificationState *,const NotificationAudience *);
void notification_announcement_clear(NotificationState *);
#endif
