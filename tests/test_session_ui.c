/* A test adapter exercises selection independently of the built-in UIs. */
#include "session.h"
#include "session_internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static unsigned entered,left,lines,prepared,received,doors,ticks,menus,prompts,helps;
static SessionResultStatus status;
static int preserve_position;
static int output(void *context,const char *text,size_t size) {
    (void)text;*(size_t *)context+=size;return 1;
}
static void enter(Session *s,int initial,int menu,Language language) {
    (void)initial;rtbbs(s)->presentation.ui.enabled=menu;rtbbs(s)->presentation.ui.language=language;entered++;
}
static void leave(Session *s) { (void)s;left++; }
static void line(Session *s,char *text,size_t size) {
    (void)s;assert(size==5 && !strcmp(text,"mixed"));lines++;
}
static int prepare(Session *s,const OperationResult *r) { (void)s;(void)r;prepared++;return preserve_position; }
static void result(Session *s,const OperationResult *r,SessionResultView view,uint64_t now,int cancelled) {
    assert(!s->core.pending && s->core.now==now && cancelled==preserve_position);
    if(r->auth>0)assert(s->core.user==(uint32_t)r->auth && s->core.user_epoch==r->user_epoch);
    if(view.status==SESSION_RESULT_FAILED)assert(!view.effects && !view.content && !view.page && !view.body && !view.attachments);
    else assert(view.effects==&r->feedback);
    status=view.status;received++;
}
static void door(Session *s) { assert(!s->connection.door);doors++; }
static void tick(Session *s,uint64_t now) { assert(s->core.now==now);ticks++; }
static int uppercase(const Session *s,const char *line,size_t size) { (void)s;(void)line;(void)size;return 1; }

static void menu(Session *s,int explicit_request) { (void)s;(void)explicit_request;menus++; }
static void prompt(Session *s) { (void)s;prompts++; }
static void help(Session *s,const char *topic) { (void)s;assert(!*topic);helps++; }

void session_ui_adapter_test(void) {
    Session *s=calloc(1,sizeof(*s));OperationResult *r=calloc(1,sizeof(*r));assert(s && r);
    size_t written=0;SessionIO io={&written,output,NULL,NULL};
    SessionUiConfig c=session_ui_default();
    assert(c.policy==SESSION_UI_FIXED && c.count==3 && c.allowed==1 && !strcmp(c.adapters[0]->name,"rtbbs"));
    assert(c.adapters[1]==session_ui_mmm());
    assert(c.adapters[2]==session_ui_mash() && c.adapters[2]!=c.adapters[1]);
    assert(c.adapters[2]->login_required);
    SessionUiConfig mash=c;
    assert(session_ui_configure(&mash,"MEMBERS","mash","rtbbs,mmm,mash"));
    assert(mash.default_index==2 && mash.allowed==7 && mash.policy==SESSION_UI_MEMBERS);
    assert(!session_ui_configure(&mash,"ALL","mash","rtbbs,mmm"));
    assert(session_ui_config_valid(&c));
    SessionUiAdapter alt=*session_ui_rtbbs();alt.name="test-alt";alt.enter=enter;alt.leave=leave;
    alt.line=line;alt.keep_position=prepare;alt.result=result;alt.door_finish=door;alt.tick=tick;alt.uppercase=uppercase;
    c.adapters[1]=&alt;c.count=2;c.allowed=3;
    assert(session_ui_config_valid(&c));
    SessionUiConfig bad=c;bad.count=0;assert(!session_ui_config_valid(&bad));
    bad=c;bad.count=SESSION_UI_MAX+1;assert(!session_ui_config_valid(&bad));
    bad=c;bad.allowed=4;assert(!session_ui_config_valid(&bad));
    bad=c;bad.allowed=2;assert(!session_ui_config_valid(&bad));
    bad=c;bad.default_index=2;assert(!session_ui_config_valid(&bad));
    bad=c;bad.policy=(SessionUiPolicy)9;assert(!session_ui_config_valid(&bad));
    bad=c;bad.adapters[1]=bad.adapters[0];assert(!session_ui_config_valid(&bad));
    SessionUiAdapter broken=alt;broken.help=NULL;bad=c;bad.adapters[1]=&broken;assert(!session_ui_config_valid(&bad));
    broken=alt;broken.name="invalid UI";assert(!session_ui_config_valid(&bad));
    assert(!session_init_ui(s,io,500,1,0,0,1,LANGUAGE_EN,&bad));assert(!s->core.active && !written);
    session_init(s,io,500,1,0,0,1,LANGUAGE_EN);
    assert(!strcmp(session_ui_name(s),"rtbbs"));
    assert(session_ui_select(s,"mmm")==SESSION_UI_DENIED);
    assert(session_ui_select(s,"unknown")==SESSION_UI_UNKNOWN);
    session_dispose(s);
    assert(session_init_ui(s,io,500,1,0,0,0,LANGUAGE_EN,&c));
    assert(session_ui_select(s,"test-alt")==SESSION_UI_DENIED);
    assert(session_ui_select(s,"rtbbs")==SESSION_UI_UNCHANGED);session_dispose(s);
    c.policy=SESSION_UI_MEMBERS;assert(session_init_ui(s,io,500,1,0,0,0,LANGUAGE_EN,&c));
    assert(session_ui_select(s,"test-alt")==SESSION_UI_DENIED);
    s->core.user=7;s->core.user_epoch=11;s->core.board_grants[1]=19;
    assert(session_ui_select(s,"test-alt")==SESSION_UI_SELECTED);
    assert(s->core.user==7 && s->core.user_epoch==11 && s->core.board_grants[1]==19);
    assert(session_ui_select(s,"rtbbs")==SESSION_UI_SELECTED && left==1);session_dispose(s);
    c.policy=SESSION_UI_ALL;c.allowed=1;assert(session_init_ui(s,io,500,1,0,0,0,LANGUAGE_EN,&c));
    assert(session_ui_select(s,"test-alt")==SESSION_UI_DENIED);session_dispose(s);
    c.allowed=3;assert(session_init_ui(s,io,500,1,0,0,0,LANGUAGE_EN,&c));
    c.allowed=1;c.policy=SESSION_UI_FIXED; /* Existing connection retains its policy snapshot. */
    assert(s->ui_binding.config.allowed==3 && s->ui_binding.config.policy==SESSION_UI_ALL);
    assert(session_ui_select(s,NULL)==SESSION_UI_INVALID);
    assert(session_ui_select(s,"MMM")==SESSION_UI_UNKNOWN);
    /* Each barrier leaves the old adapter and state intact. */
#define BUSY(field,value) do { (field)=(value);assert(session_ui_select(s,"test-alt")==SESSION_UI_BUSY);assert(!strcmp(session_ui_name(s),"rtbbs"));(field)=0; } while(0)
    BUSY(s->core.pending,1);BUSY(s->core.wake,123);BUSY(s->core.closing,1);
    BUSY(rtbbs(s)->auth.password_mode,'l');BUSY(rtbbs(s)->auth.application.step,1);
    BUSY(rtbbs(s)->presentation.ui.input,UI_LOGIN);BUSY(rtbbs(s)->presentation.ui.view,UI_ARTICLE);
    BUSY(rtbbs(s)->presentation.classic_input,1);BUSY(rtbbs(s)->presentation.classic_area,1);BUSY(rtbbs(s)->presentation.game_menu,1);
    BUSY(rtbbs(s)->presentation.page[0],'x');BUSY(rtbbs(s)->presentation.preferences.exit_action,PREF_EXIT_QUIT);
    BUSY(rtbbs(s)->editor.editing,1);BUSY(rtbbs(s)->editor.editor_upload,1);BUSY(rtbbs(s)->editor.editor_confirm,1);BUSY(rtbbs(s)->editor.editor_upload_question,1);
    BUSY(rtbbs(s)->reading.reader.active,1);BUSY(rtbbs(s)->reading.reader.automatic,1);BUSY(rtbbs(s)->reading.check.active,1);
    BUSY(rtbbs(s)->reading.profile_walk.pending,1);BUSY(rtbbs(s)->reading.report.pending,1);
    BUSY(rtbbs(s)->files.vault_mode,1);BUSY(rtbbs(s)->files.batch_screen,1);BUSY(rtbbs(s)->files.batch_copying,1);BUSY(rtbbs(s)->files.file_selecting,1);
    BUSY(rtbbs(s)->mail_state.mail_mode,1);BUSY(rtbbs(s)->mail_state.mail_input,1);BUSY(rtbbs(s)->mail_state.mail_pending,1);BUSY(rtbbs(s)->mail_state.mail.delete_id,1);
    BUSY(rtbbs(s)->conversation.chatting,1);BUSY(rtbbs(s)->conversation.gather.mode,1);BUSY(rtbbs(s)->conversation.outside_talk,1);
    BUSY(rtbbs(s)->conversation.talk_select,1);BUSY(rtbbs(s)->conversation.talk_once,1);BUSY(rtbbs(s)->conversation.memo_input,1);
    BUSY(rtbbs(s)->conversation.status_pending,1);BUSY(rtbbs(s)->conversation.chat_pending,1);
    BUSY(s->connection.door,1);BUSY(s->connection.zmodem,1);BUSY(s->connection.zmodem_start,1);
    BUSY(s->connection.binary,1);BUSY(s->connection.connector_literal,1);BUSY(s->connection.binary_reader.used,1);
#undef BUSY
    assert(connector_block_begin(&s->connection.connector_block,CONNECTOR_FILE_BLOCK,2,10)==1);
    assert(session_ui_select(s,"test-alt")==SESSION_UI_BUSY);connector_block_clear(&s->connection.connector_block);
    strcpy(rtbbs(s)->editor.draft.body,"retained buffer");s->connection.rows=35;
    assert(session_ui_select(s,"test-alt")==SESSION_UI_SELECTED && !s->core.user);
    assert(!rtbbs(s)->editor.draft.body[0] && s->connection.rows==35); /* private state, common terminal size */
    assert(session_ui_select(s,"rtbbs")==SESSION_UI_SELECTED);
    assert(!strcmp(rtbbs(s)->editor.draft.body,"retained buffer"));
    /* Simulate an explicit user selection, retained across login restoration. */
    assert(session_ui_choose(s,"test-alt")==SESSION_UI_SELECTED);
    assert(session_line(s,"mixed",5,100) && lines==1); /* No RTBBS uppercase/command mapping. */
    assert(session_uppercase(s,"mixed",5));rtbbs(s)->auth.password_mode='l';assert(!session_uppercase(s,"mixed",5));rtbbs(s)->auth.password_mode=0;
    s->connection.door=1;session_door_finish(s);assert(doors==1);
    session_tick(s,101);assert(ticks==1);
    s->core.pending=1;r->session=499;r->slot=1;r->auth=7;r->user_epoch=11;
    session_result(s,r,102);assert(!prepared && !received && !s->core.user && s->core.pending);
    r->session=500;r->slot=2;session_result(s,r,102);assert(!prepared && !received);
    r->slot=1;session_result(s,r,103);assert(prepared==1 && received==1 && s->core.user==7 && status==SESSION_RESULT_OK);
    r->auth=0;r->feedback.failed=1;r->selected=45;r->content.kind=1;
    session_result(s,r,104);assert(received==2 && status==SESSION_RESULT_FAILED && !s->core.current);
    r->feedback.failed=0;preserve_position=1;session_result(s,r,105);assert(!s->core.current);
    preserve_position=0;session_result(s,r,106);assert(s->core.current==45);
    r->revoked=1;unsigned before=received;session_result(s,r,107);
    assert(received==before && !s->core.user && s->core.closing);
    s->connection.door=1;session_door_finish(s);assert(doors==1 && !s->connection.door);
    session_dispose(s);
    c=session_ui_default();c.count=2;c.adapters[1]=&alt;c.allowed=3;c.default_index=1;
    assert(session_init_ui(s,io,501,1,0,0,0,LANGUAGE_JA,&c));
    assert(!strcmp(session_ui_name(s),"test-alt"));
    assert(connector_block_begin(&s->connection.connector_block,CONNECTOR_FILE_BLOCK,2,10)==1);
    before=ticks;session_tick(s,UINT64_MAX);assert(!s->core.active && ticks==before);
    session_dispose(s);
    /* Reuse only the RTBBS input adapter; its UI events go to the selected renderer. */
    SessionUiAdapter renderer=alt;renderer.line=session_ui_rtbbs()->line;
    renderer.menu=menu;renderer.prompt=prompt;renderer.help=help;
    c.adapters[1]=&renderer;
    assert(session_init_ui(s,io,502,1,0,0,1,LANGUAGE_EN,&c));
    assert(session_line(s,"HELP",4,200) && helps==1 && prompts);
    assert(session_line(s,"MENU",4,201) && menus==1);
    session_dispose(s);
    for(int language=LANGUAGE_JA;language<LANGUAGE_COUNT;language++) {
        session_init(s,io,503,1,0,0,1,(Language)language);
        char message[900];
        assert(session_notice(s,SESSION_NOTICE_DOOR,0,0,0,message,sizeof(message)));
        assert(strstr(message,language==LANGUAGE_JA?"ゲーム開始":"Game started"));
        assert(session_notice(s,SESSION_NOTICE_PROGRESS,50,10,20,message,sizeof(message)) && strstr(message,"50%") && strstr(message,"10 / 20"));
        assert(!session_notice(s,SESSION_NOTICE_DOOR,0,0,0,message,2));
        rtbbs(s)->conversation.chat_notice=1;rtbbs(s)->conversation.chat_prompting=1;
        assert(session_ui_snapshot(s).redraw);
        rtbbs(s)->auth.password_mode='a';assert(!session_ui_snapshot(s).redraw && session_secret(s));
        rtbbs(s)->auth.password_mode=0;rtbbs(s)->presentation.ui.enabled=0;
        assert(session_notice(s,SESSION_NOTICE_IDLE_CLOSED,0,0,0,message,sizeof(message)) && !strcmp(message,"BYE idle timeout\r\n"));
        session_dispose(s);assert(!s->ui_binding.state);
        for(unsigned index=0;index<SESSION_UI_MAX;index++)assert(!s->ui_binding.states[index]);
    }
    free(r);free(s);
    puts("PASS: RTBBS registry, policy snapshot, safe selection, selected UI routing and Core-before-UI effects");
}
