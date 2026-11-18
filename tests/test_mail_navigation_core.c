#include "mail_navigation_core.h"
#include "store_limits.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void listings(void) {
    MailNavigation s={.current=73,.reading=1,.delete_id=72},other={0};
    assert(mail_navigation_filter(&s,MAIL_LIST_SEARCH,"Mixed 日本語",19));
    MailListing plan=mail_navigation_listing(&s);
    assert(plan.filter==MAIL_LIST_SEARCH && plan.cursor==19 && !strcmp(plan.query,"Mixed 日本語"));
    plan.query[0]='X';assert(!strcmp(s.query,"Mixed 日本語"));
    for(int mode=MAIL_LIST_ALL;mode<=MAIL_LIST_NEWS;mode++) {
        assert(mail_navigation_filter(&s,(MailListMode)mode,"filter",23));
        plan=mail_navigation_listing(&s);
        assert(plan.filter==(MailListMode)mode && plan.cursor==23);
        assert(!strcmp(plan.query,mode>=MAIL_LIST_SEARCH?"filter":""));
        mail_navigation_page(&s,MAIL_EVENT_LIST,31);
        assert(mail_navigation_listing(&s).cursor==31);
        mail_navigation_page(&s,MAIL_EVENT_READ,0);assert(s.cursor==31);
        mail_navigation_page(&s,MAIL_EVENT_FILES,0);assert(s.cursor==0);
        assert(s.current==73 && s.reading && s.delete_id==72);
    }
    char limit[82];memset(limit,'a',80);limit[80]=0;
    assert(mail_navigation_filter(&s,MAIL_LIST_SEARCH,limit,UINT32_MAX));
    MailNavigation saved=s;
    limit[80]='a';limit[81]=0;
    const char *bad[]={NULL,"",limit,"bad\tquery","bad\rquery","bad\177query"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        assert(!mail_navigation_filter(&s,MAIL_LIST_SEARCH,bad[i],0));
        assert(!memcmp(&s,&saved,sizeof(s)));
    }
    assert(!mail_navigation_filter(&s,(MailListMode)-1,"x",0));
    assert(!mail_navigation_filter(&s,(MailListMode)6,"x",0));
    assert(!memcmp(&s,&saved,sizeof(s)));
    assert(mail_navigation_filter(&s,MAIL_LIST_NEWS,"2026-11-19",42));
    assert(mail_navigation_filter(&s,MAIL_LIST_NEWS,s.query,43));
    plan=mail_navigation_listing(&s);assert(plan.cursor==43 && !strcmp(plan.query,"2026-11-19"));
    mail_navigation_clear_filter(&s);
    assert(s.current==73 && s.reading && s.delete_id==72 && !s.cursor && s.filter==MAIL_LIST_ALL);
    for(unsigned i=0;i<sizeof(s.query);i++)assert(!s.query[i]);
    assert(!other.current && !other.cursor && !other.reading && !other.query[0]);
}
static void selection(void) {
    for(int e=MAIL_EVENT_OTHER;e<=MAIL_EVENT_ATTACH;e++) {
        MailNavigation s={.current=42,.cursor=19,.reading=1};
        mail_navigation_observe(&s,(MailSessionEvent)e,0);
        assert(s.current==42 && s.cursor==19);
        assert(s.reading==(e!=MAIL_EVENT_LIST && e!=MAIL_EVENT_FILES));
        mail_navigation_observe(&s,(MailSessionEvent)e,51);
        assert(s.current==51 && s.cursor==19);
        assert(s.reading==(e!=MAIL_EVENT_LIST && e!=MAIL_EVENT_FILES && e!=MAIL_EVENT_SEND));
    }
    MailNavigation s={.current=42,.reading=1};
    mail_navigation_confirm(&s,42);assert(s.delete_id==42);
    assert(!mail_navigation_confirmed(&s,0) && !s.delete_id && s.current==42);
    mail_navigation_confirm(&s,43);assert(mail_navigation_confirmed(&s,1)==43 && !s.delete_id);
    assert(!mail_navigation_confirmed(&s,1));
    mail_navigation_confirm(&s,42);mail_navigation_observe(&s,MAIL_EVENT_READ,99);
    assert(mail_navigation_confirmed(&s,1)==42 && s.current==99);
    mail_navigation_observe(&s,MAIL_EVENT_OTHER,0);assert(s.current==99 && s.reading);
    ReaderPosition p=mail_navigation_start(0);assert(p.cursor==1 && !p.descending);
    p=mail_navigation_start(1);assert(p.cursor==UINT32_MAX && p.descending);
    p=mail_navigation_move(&s,READER_PREVIOUS,0,9);assert(p.cursor==98 && p.descending);
    p=mail_navigation_move(&s,READER_PREVIOUS,1,9);assert(p.cursor==100 && !p.descending);
    p=mail_navigation_move(&s,READER_REPEAT,1,9);assert(p.cursor==99 && p.descending);
    p=mail_navigation_move(&s,READER_FORWARD,1,5);assert(p.cursor==104 && !p.descending);
    p=mail_navigation_move(&s,READER_BACKWARD,0,5);assert(p.cursor==94 && p.descending);
    s.current=1;p=mail_navigation_move(&s,READER_BACKWARD,0,1);assert(!p.cursor);
    s.current=CONTENT_ID_MAX;p=mail_navigation_move(&s,READER_FORWARD,0,1);assert(!p.cursor);
    p=mail_navigation_move(&s,READER_BACKWARD,0,UINT32_MAX);assert(!p.cursor);
}
static void drafts(void) {
    unsigned cases=0;
    for(int e=MAIL_EVENT_OTHER;e<=MAIL_EVENT_ATTACH;e++)
    for(unsigned recipient=0;recipient<2;recipient++)
    for(unsigned id=0;id<2;id++)
    for(unsigned revision=0;revision<2;revision++) {
        MailDraftEffect expected=MAIL_DRAFT_KEEP;
        if((e==MAIL_EVENT_BEGIN || e==MAIL_EVENT_REPLY || e==MAIL_EVENT_RESUME) && recipient)expected=MAIL_DRAFT_OPEN;
        if(e==MAIL_EVENT_ATTACH && recipient)expected=MAIL_DRAFT_ATTACH;
        if(e==MAIL_EVENT_SAVE && revision)expected=MAIL_DRAFT_SAVED;
        if(e==MAIL_EVENT_SEND)expected=id?MAIL_DRAFT_SENT:MAIL_DRAFT_RETRY;
        assert(mail_draft_effect((MailSessionEvent)e,recipient?UINT32_MAX:0,id?UINT32_MAX:0,revision?UINT32_MAX:0)==expected);
        cases++;
    }
    assert(cases==80);
}
int main(void) {
    listings();selection();drafts();
    puts("PASS: mail query/page isolation, invalid/boundary input, selection/confirmation, reader boundaries and 80 draft outcomes");
    return 0;
}
