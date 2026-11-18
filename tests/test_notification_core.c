#include "notification_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static NotificationAudience member(uint32_t user) {
    return (NotificationAudience){.user=user,.active=1,.interactive=1};
}
static void private_mail(void) {
    NotificationState state={0},other={0};NotificationAudience a=member(7),b=member(8);
    notification_mail_arrived(&state,&a,8);notification_mail_arrived(&state,&a,0);
    assert(!state.mail.arrivals);
    notification_mail_arrived(&state,&a,7);notification_mail_arrived(&state,&a,7);
    notification_mail_arrived(&other,&b,7);assert(state.mail.arrivals==2 && !other.mail.arrivals);
    a.user=0;assert(!notification_mail_take(&state,&a) && state.mail.arrivals==2);
    a=member(7);assert(notification_mail_take(&state,&a)==2 && !notification_mail_take(&state,&a));
    for(unsigned mode=0;mode<3;mode++) {
        a=member(7);if(mode==0)a.active=0;else if(mode==1)a.closing=1;else a.interactive=0;
        notification_mail_arrived(&state,&a,7);assert(!state.mail.arrivals);
        state.mail.arrivals=1;assert(!notification_mail_take(&state,&a) && state.mail.arrivals==1);
        notification_mail_discard(&state);
    }
    a=member(7);state.mail.arrivals=UINT32_MAX-1;
    notification_mail_arrived(&state,&a,7);notification_mail_arrived(&state,&a,7);
    assert(state.mail.arrivals==UINT32_MAX && notification_mail_take(&state,&a)==UINT32_MAX);
    a.interactive=0;a.restore_interactive=1;notification_mail_arrived(&state,&a,7);assert(!state.mail.arrivals);
    a.blocked=NOTICE_BINARY;notification_mail_arrived(&state,&a,7);assert(state.mail.arrivals==1);
    assert(!notification_mail_take(&state,&a));a.interactive=1;a.blocked=0;
    assert(notification_mail_take(&state,&a)==1);
    a.interactive=0;a.restore_interactive=0;a.blocked=NOTICE_BINARY;
    notification_mail_arrived(&state,&a,7);assert(!state.mail.arrivals);
}
static void preferences(void) {
    NotificationState state={0};NotificationAudience a=member(7);
    notification_mail_arrived(&state,&a,7);
    notification_mail_preference(&state,7,8,1,10);notification_mail_preference(&state,0,0,1,10);
    assert(state.mail.arrivals==1 && !state.mail.denied && !state.mail.revision);
    notification_mail_preference(&state,7,7,1,10);
    assert(!state.mail.arrivals && state.mail.denied && state.mail.revision==10);
    notification_mail_arrived(&state,&a,7);assert(!state.mail.arrivals);
    notification_mail_preference(&state,7,7,0,9);assert(state.mail.denied && state.mail.revision==10);
    notification_mail_preference(&state,7,7,0,11);assert(!state.mail.denied && !state.mail.arrivals);
    notification_mail_arrived(&state,&a,7);
    notification_mail_preference(&state,7,7,0,11);assert(state.mail.arrivals==1); /* replay is harmless */
    assert(notification_announce(&state,&a,"public"));notification_mail_discard(&state);
    assert(!state.mail.arrivals && state.mail.revision==11 && *state.announcements);
    notification_mail_preference(&state,7,7,1,12);notification_mail_reset(&state);
    assert(!state.mail.denied && !state.mail.revision && !state.mail.arrivals && *state.announcements);
    a.user=8;notification_mail_preference(&state,8,7,1,99);notification_mail_arrived(&state,&a,7);
    assert(!state.mail.denied && !state.mail.arrivals);
}
static void delivery_matrix(void) {
    const unsigned mail_mask=NOTICE_DOOR|NOTICE_BINARY|NOTICE_TRANSFER|NOTICE_SECRET|NOTICE_PENDING|NOTICE_PAGE|
        NOTICE_EDITOR|NOTICE_TIMER|NOTICE_INPUT|NOTICE_CHAT|NOTICE_SCAN;
    const unsigned announcement_mask=NOTICE_DOOR|NOTICE_BINARY|NOTICE_TRANSFER|NOTICE_SECRET|NOTICE_PENDING|NOTICE_PAGE;
    /* All combinations: an unrelated blocker must not accidentally enable output. */
    for(unsigned flags=0;flags<=mail_mask;flags++) {
        NotificationState state={0};NotificationAudience a=member(7);a.blocked=flags;
        notification_mail_arrived(&state,&a,7);assert(state.mail.arrivals==1);
        assert(notification_announce(&state,&a,"notice"));
        assert((notification_announcement_peek(&state,&a)!=NULL)==!(flags&announcement_mask));
        assert(notification_mail_take(&state,&a)==(flags?0u:1u));
        a.blocked=0;assert(notification_mail_take(&state,&a)==(flags?1u:0u));
        assert(!strcmp(notification_announcement_peek(&state,&a),"notice\r\n"));
        notification_announcement_clear(&state);assert(!notification_announcement_peek(&state,&a));
    }
}
static void announcements(void) {
    NotificationState state={0},other={0};NotificationAudience a=member(7);
    for(unsigned mode=0;mode<3;mode++) {
        a=member(7);if(mode==0)a.active=0;else if(mode==1)a.closing=1;else a.interactive=0;
        assert(!notification_announce(&state,&a,"rejected") && !*state.announcements);
    }
    a=member(0);assert(notification_announce(&state,&a,"guest notice"));
    assert(notification_announce(&state,&a,"次のお知らせ"));
    assert(!strcmp(state.announcements,"guest notice\r\n次のお知らせ\r\n"));
    assert(!notification_announcement_peek(&other,&a));
    a.closing=1;assert(notification_announcement_peek(&state,&a)); /* accepted shutdown notice */
    notification_announcement_clear(&state);a=member(7);
    char text[4097];memset(text,'x',sizeof(text));text[4096]=0;
    assert(!notification_announce(&state,&a,text) && !*state.announcements);
    text[4093]=0;assert(!notification_announce(&state,&a,text));
    text[4092]=0;assert(notification_announce(&state,&a,text));
    char before[4096];memcpy(before,state.announcements,sizeof(before));
    assert(!notification_announce(&state,&a,"extra"));assert(!memcmp(before,state.announcements,sizeof(before)));
    notification_announcement_clear(&state);assert(!notification_announcement_peek(&state,&a));
    assert(notification_announce(&state,&a,"") && !strcmp(state.announcements,"\r\n"));
}
int main(void) {
    private_mail();preferences();delivery_matrix();announcements();
    puts("PASS notification Core: recipient isolation, revision/deny/reset, saturation, 2048 busy-state combinations, announcement bounds and lifetime");
    return 0;
}
