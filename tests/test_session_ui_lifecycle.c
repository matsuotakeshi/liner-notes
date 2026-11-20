/* Real Session/registry/Core, fake worker queue and tiny non-RTBBS UI state.
 * No Session internals or RTBBS state headers are linked into this unit test. */
#include "session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { SessionUiSnapshot view;unsigned events;char draft[32]; } State;
static unsigned cleaned,discarded,revoked,delivered,requests;
static State *state(Session *s) { return s->ui_binding.state; }
static SessionUiSnapshot snapshot(const Session *s) { return ((const State *)s->ui_binding.state)->view; }
static void enter(Session *s,int initial,int menu,Language language) { (void)initial;state(s)->view.menu=menu;state(s)->view.language=language; }
static void cleanup(Session *s,int reason,int notify) {
    (void)notify;assert(s->ui_binding.state);
    if(reason==2)discarded++;else if(reason==1) { assert(!s->core.user && s->core.closing);revoked++; }
    cleaned++;memset(state(s),0,sizeof(State));
}
static void simple(Session *s) { state(s)->events++; }
static int ready(const Session *s) { return !snapshot(s).editing; }
static void line(Session *s,char *text,size_t size) { assert(size==5 && !strcmp(text,"hello"));simple(s); }
static int connector(Session *s,char *text,size_t size) { (void)s;(void)text;(void)size;return 0; }
static int binary(Session *s,unsigned char byte,uint64_t now) { (void)byte;(void)now;simple(s);return 1; }
static void prepare(Session *s,OperationRequest *q) { (void)q;simple(s); }
static int prefs(const Session *s,OperationRequest *q) {
    if(q) { memset(q,0,sizeof(*q));q->user=s->core.user;q->user_epoch=s->core.user_epoch; }
    return s->core.user!=0;
}
static uint64_t signature(const Session *s) { return ((const State *)s->ui_binding.state)->events; }
static void text(Session *s,const char *value) { (void)value;simple(s); }
static void menu(Session *s,int explicit_request) { (void)explicit_request;simple(s); }
static void tick(Session *s,uint64_t now) { assert(s->core.now==now);simple(s); }
static int upper(const Session *s,const char *text,size_t size) { (void)s;(void)text;(void)size;return 1; }
static int keep(Session *s,const OperationResult *r) { (void)s;(void)r;return 0; }
static void result(Session *s,const OperationResult *r,SessionResultView view,uint64_t now,int cancelled) {
    (void)cancelled;assert(s->core.now==now && !s->core.pending);
    if(r->auth>0)assert(s->core.user==(uint32_t)r->auth && s->core.user_epoch==r->user_epoch);
    assert(view.status==SESSION_RESULT_OK);delivered++;
}
static int chat_event(Session *s,const ChatEvent *event) { (void)event;simple(s);return 1; }
static void notify(Session *s,MessageId id,uint32_t number,const char *body) { (void)id;(void)number;(void)body;simple(s); }
static void arrived(Session *s,uint32_t user) { (void)user;simple(s); }
static void mail_notify(Session *s,uint32_t user,int deny,uint32_t revision) { (void)user;(void)deny;(void)revision;simple(s); }
static void finish(Session *s,int ok,unsigned id,uint64_t size,int recovery) {
    (void)ok;(void)id;(void)size;(void)recovery;assert(!s->connection.zmodem && !s->connection.zmodem_start);simple(s);
}
static int notice(const Session *s,SessionNotice kind,unsigned number,uint64_t bytes,uint64_t total,char *out,size_t size) {
    (void)s;(void)kind;(void)number;(void)bytes;(void)total;
    if(size<5)return 0;
    strcpy(out,"test");return 1;
}
static int write_bytes(void *context,const char *text,size_t size) { (void)context;(void)text;(void)size;return 1; }
/* Fake compiled default and worker queue replace only external ports. */
static const SessionUiAdapter adapter={
    .name="first",.state_size=sizeof(State),.snapshot=snapshot,.cleanup=cleanup,
    .connector_line=connector,.binary_byte=binary,.prepare_request=prepare,.preferences=prefs,.signature=signature,
    .scan_pause=simple,.scan_stop=simple,.board_refresh=simple,.access_refresh=simple,.chat_kick=simple,.chat_event=chat_event,
    .announce=text,.notify=notify,.mail_arrived=arrived,.mail_notify=mail_notify,.transfer_receipt=text,.transfer_finish=finish,
    .notice=notice,.enter=enter,.leave=simple,.can_leave=ready,.line=line,.keep_position=keep,.result=result,
    .emit=text,.prompt=simple,.menu=menu,.help=text,.uppercase=upper,.tick=tick,.door_finish=simple
};
const SessionUiAdapter *session_ui_rtbbs(void) { return &adapter; }
int board_submit(const BoardRequest *q) { assert(q->user==12 && q->user_epoch==3 && q->session==77 && q->slot==2);requests++;return 1; }

int main(void) {
    Session s;SessionIO io={NULL,write_bytes,NULL,NULL};
    SessionUiAdapter other=adapter;other.name="second";
    SessionUiConfig config=session_ui_default();config.adapters[1]=&other;config.count=2;config.allowed=3;config.policy=SESSION_UI_ALL;
    SessionUiConfig parsed=config;
    assert(session_ui_configure(&parsed,"MEMBERS","second","first,second"));
    assert(parsed.policy==SESSION_UI_MEMBERS && parsed.default_index==1 && parsed.allowed==3);
    assert(!session_ui_configure(&parsed,"ALL","first","second"));
    assert(!session_ui_configure(&parsed,"ALL","first","first,first"));
    assert(!session_ui_configure(&parsed,"ALL","first","first,"));
    assert(!session_ui_configure(&parsed,"ALL","missing","first"));
    assert(!session_ui_configure(&parsed,"ALL","first","mmm"));
    assert(parsed.policy==SESSION_UI_MEMBERS && parsed.default_index==1);
    assert(session_init_ui(&s,io,77,2,1,1,1,LANGUAGE_EN,&config));
    assert(sizeof(State)<128 && s.ui_binding.state && !s.ui_binding.states[1]);
    State *first=state(&s);strcpy(first->draft,"private to first");
    assert(session_ui_choose(&s,"second")==SESSION_UI_SELECTED && state(&s)!=first && !state(&s)->draft[0]);
    assert(snapshot(&s).menu && snapshot(&s).language==LANGUAGE_EN);
    strcpy(state(&s)->draft,"private to second");
    assert(session_ui_select(&s,"first")==SESSION_UI_SELECTED && state(&s)==first && !strcmp(first->draft,"private to first"));
    assert(session_ui_select(&s,"second")==SESSION_UI_SELECTED && !strcmp(state(&s)->draft,"private to second"));
    s.core.user=12;s.core.user_epoch=3;
    ChatEvent event={0};unsigned before=state(&s)->events,old=first->events;
    assert(session_chat_event(&s,&event));assert(state(&s)->events==before+1 && first->events==old);
    state(&s)->view.secret=1;assert(session_secret(&s) && !session_uppercase(&s,"x",1));
    assert(session_ui_select(&s,"first")==SESSION_UI_BUSY);state(&s)->view.secret=0;
    state(&s)->view.editing=1;assert(session_ui_select(&s,"first")==SESSION_UI_BUSY);state(&s)->view.editing=0;
    before=state(&s)->events;
    session_scan_pause(&s);session_scan_stop(&s);session_board_refresh(&s);session_access_refresh(&s);
    session_announce(&s,"hello");session_notify(&s,MSG_ADMIN_NOTICE,0,"hello");session_mail_arrived(&s,12);
    session_mail_notify(&s,12,0,1);session_transfer_receipt(&s,"receipt");session_tick(&s,50);
    assert(session_line(&s,"hello",5,51));session_door_finish(&s);
    s.connection.zmodem=s.connection.zmodem_start=1;session_zmodem_finish(&s,1,4,8,0);
    assert(state(&s)->events==before+13 && first->events==old);
    char out[32];assert(session_notice(&s,SESSION_NOTICE_DOOR,0,0,0,out,sizeof(out)) && !strcmp(out,"test"));
    OperationRequest *q=calloc(1,sizeof(*q));OperationResult *r=calloc(1,sizeof(*r));assert(q && r);
    assert(session_disconnect_preferences(&s,q) && q->user==12 && q->user_epoch==3);
    q->user=999;q->user_epoch=999;q->op='P';assert(session_submit(&s,q)==SESSION_OPERATION_ACCEPTED && requests==1);
    r->session=78;r->slot=2;r->auth=9;r->user_epoch=4;
    session_result(&s,r,52);assert(!delivered && s.core.user==12 && s.ui_binding.states[0]);
    r->session=77;session_result(&s,r,53);assert(delivered==1 && s.core.user==9 && !s.ui_binding.states[0] && discarded==1);
    assert(session_ui_select(&s,"first")==SESSION_UI_SELECTED && !state(&s)->draft[0]);
    s.ui_binding.restore_pending=1;
    assert(session_logout(&s) && !s.core.user && !s.ui_binding.states[1] && discarded==2);
    assert(!s.ui_binding.restore_pending && !s.ui_binding.preferred[0] && !s.ui_binding.explicit_choice);
    assert(!session_chat_event(&s,&event));
    assert(session_ui_select(&s,"second")==SESSION_UI_SELECTED);
    s.core.user=9;session_revoke(&s);assert(!s.core.user && s.core.closing && !s.ui_binding.states[0] && revoked==1);
    session_dispose(&s);assert(!s.ui_binding.state && !s.ui_binding.active && cleaned==5);
    for(unsigned i=0;i<SESSION_UI_MAX;i++)assert(!s.ui_binding.states[i]);
    session_dispose(&s);assert(cleaned==5); /* idempotent teardown */
    config.count=0;assert(!session_init_ui(&s,io,78,0,1,1,1,LANGUAGE_EN,&config));
    assert(!s.core.active && !session_disconnect_preferences(&s,q));session_dispose(&s);
    config.count=2;
    for(unsigned mode=0;mode<5;mode++) {
        config.policy=mode==2?SESSION_UI_FIXED:SESSION_UI_MEMBERS;
        config.allowed=mode==3?1:3;
        assert(session_init_ui(&s,io,77,2,1,1,1,LANGUAGE_EN,&config));
        if(mode==4) { s.ui_binding.config.policy=SESSION_UI_ALL;assert(session_ui_choose(&s,"first")==SESSION_UI_UNCHANGED); }
        memset(r,0,sizeof(*r));r->session=77;r->slot=2;r->auth=12;r->user_epoch=3;
        strcpy(r->ui_profile,mode==1?"missing":"second");
        session_result(&s,r,100);
        assert(!strcmp(session_ui_name(&s),mode==0?"second":"first"));
        assert(!strcmp(s.ui_binding.preferred,r->ui_profile)); /* restriction does not erase preference */
        if(mode==0) {
            assert(session_ui_save_choice(&s,"second") && s.core.pending);
            r->auth=0;r->feedback.ui_profile_saved=1;session_result(&s,r,101);
            assert(!s.core.pending && !strcmp(s.ui_binding.preferred,"second"));
        }
        session_dispose(&s);
    }
    free(q);free(r);puts("PASS: opaque UI ownership, independent routing, identity invalidation, teardown and typed Core operations");return 0;
}
