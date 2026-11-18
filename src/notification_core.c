#include "notification_core.h"
#include <string.h>
static const unsigned announcement_blocks=NOTICE_DOOR|NOTICE_BINARY|NOTICE_TRANSFER|NOTICE_SECRET|NOTICE_PENDING|NOTICE_PAGE;
static const unsigned mail_blocks=NOTICE_DOOR|NOTICE_BINARY|NOTICE_TRANSFER|NOTICE_SECRET|NOTICE_PENDING|NOTICE_PAGE|
    NOTICE_EDITOR|NOTICE_TIMER|NOTICE_INPUT|NOTICE_CHAT|NOTICE_SCAN;
void notification_mail_reset(NotificationState *state) { memset(&state->mail,0,sizeof(state->mail)); }
void notification_mail_discard(NotificationState *state) { state->mail.arrivals=0; }
void notification_mail_preference(NotificationState *state,uint32_t current,uint32_t user,int deny,uint32_t revision) {
    if(!user || current!=user || revision<state->mail.revision)return;
    state->mail.revision=revision;state->mail.denied=deny;
    if(deny)notification_mail_discard(state);
}
void notification_mail_arrived(NotificationState *state,const NotificationAudience *audience,uint32_t recipient) {
    if(!state->mail.denied && audience->active && !audience->closing && recipient && audience->user==recipient &&
       (audience->interactive || ((audience->blocked&NOTICE_BINARY) && audience->restore_interactive)) &&
       state->mail.arrivals<UINT32_MAX)state->mail.arrivals++;
}
uint32_t notification_mail_take(NotificationState *state,const NotificationAudience *audience) {
    if(state->mail.denied || !audience->user || !audience->interactive || !audience->active || audience->closing ||
       (audience->blocked&mail_blocks))return 0;
    uint32_t count=state->mail.arrivals;notification_mail_discard(state);return count;
}
int notification_announce(NotificationState *state,const NotificationAudience *audience,const char *text) {
    if(!audience->active || audience->closing || !audience->interactive)return 0;
    size_t used=strlen(state->announcements),size=strlen(text);
    /* Preserve the previous queue's spare byte and all-or-nothing append. */
    if(used+3>=sizeof(state->announcements) || size>=sizeof(state->announcements)-used-3)return 0;
    memcpy(state->announcements+used,text,size);strcpy(state->announcements+used+size,"\r\n");return 1;
}
const char *notification_announcement_peek(const NotificationState *state,const NotificationAudience *audience) {
    /* Existing accepted announcements may drain while the session is closing. */
    return *state->announcements && !(audience->blocked&announcement_blocks)?state->announcements:NULL;
}
void notification_announcement_clear(NotificationState *state) { state->announcements[0]=0; }
