/* No sockets: demonstrate the reusable session and terminal contracts. */
#include "session.h"
#include "terminal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { unsigned char data[65536]; size_t size; int fail; } Memory;
static int bytes(void *ctx,const unsigned char *p,size_t n) {
    Memory *m=ctx; if(m->fail || n>=sizeof(m->data)-m->size) return 0;
    memcpy(m->data+m->size,p,n); m->size+=n; m->data[m->size]=0; return 1;
}
static int text(void *ctx,const char *p,size_t n) { return bytes(ctx,(const unsigned char *)p,n); }
static void reset(Memory *m) { m->size=0; m->data[0]=0; }
static void feed(Telnet *t,const unsigned char *p,size_t n) {
    unsigned char b; for(size_t i=0;i<n;i++) assert(telnet_receive(t,p[i],&b)==TN_NONE);
}
int main(void) {
    Memory m={0}; Telnet t;
    assert(telnet_init(&t,bytes,&m)); assert(m.size==18); reset(&m);
    const unsigned char nvt[]={'a','\n','b','\r','c','\r','\n',255};
    const unsigned char expected[]={'a','\r','\n','b','\r',0,'c','\r','\n',255,255};
    assert(telnet_write(&t,nvt,sizeof(nvt))); assert(m.size==sizeof(expected) && !memcmp(m.data,expected,m.size)); reset(&m);
    const unsigned char ack[]={255,253,1,255,253,3,255,253,0,255,251,3,255,251,0,255,251,31};
    feed(&t,ack,sizeof(ack)); feed(&t,ack,sizeof(ack)); assert(m.size==0 && telnet_echo(&t));
    const unsigned char naws[]={255,250,31,0,255,255,0,24,255,240};
    feed(&t,naws,sizeof(naws)); assert(t.width==255 && t.height==24);
    assert(telnet_write(&t,nvt,sizeof(nvt))); assert(m.size==sizeof(nvt)+1); reset(&m);
    const unsigned char reject[]={255,253,99,255,251,99};
    feed(&t,reject,sizeof(reject)); assert(m.size==6); feed(&t,reject,sizeof(reject)); assert(m.size==6); reset(&m);
    const unsigned char noecho[]={255,254,1}; feed(&t,noecho,sizeof(noecho)); assert(!telnet_echo(&t) && m.size==3);
    feed(&t,noecho,sizeof(noecho)); assert(m.size==3);
    unsigned char b=0; assert(telnet_receive(&t,255,&b)==0); assert(telnet_receive(&t,255,&b)==TN_DATA && b==255);
    puts("PASS: Telnet negotiation, duplicate suppression, NAWS escaping, NVT/binary output");

    Terminal term; reset(&m); assert(terminal_init(&term,TERMINAL_PLAIN,bytes,&m));
    const unsigned char input[]="ECHO 日\177本\r";
    int r=0; for(size_t i=0;i<sizeof(input)-1;i++) r=terminal_receive(&term,input[i],0);
    assert(r==1 && !strcmp(term.line,"ECHO 本")); terminal_clear(&term);
    assert(terminal_receive(&term,0,0)==0 && term.size==0);
    reset(&m); const unsigned char password[]="hidden-secret\b!\n";
    for(size_t i=0;i<sizeof(password)-1;i++) r=terminal_receive(&term,password[i],1);
    assert(r==1 && !strcmp(term.line,"hidden-secre!")); assert(m.size==2 && !memcmp(m.data,"\r\n",2));
    terminal_clear(&term); for(size_t i=0;i<sizeof(term.line);i++) assert(term.line[i]==0);
    assert(terminal_init(&term,TERMINAL_RAW,bytes,&m));
    for(int i=0;i<INPUT_LINE_MAX;i++) assert(terminal_receive(&term,'a',0)==0);
    assert(terminal_receive(&term,'\r',0)==0 && terminal_receive(&term,'\n',0)==1 && term.size==INPUT_LINE_MAX);
    terminal_clear(&term); m.fail=1; assert(!terminal_write(&term,(const unsigned char *)"x",1)); m.fail=0;
    puts("PASS: plain terminal adapter, UTF-8 erase, secret input, cleansing and raw maximum-length CRLF boundary");

    Session s; SessionIO io={&m,text,NULL,NULL}; reset(&m);
    session_init(&s,io,77,2,0,0,1,LANGUAGE_EN); assert(strstr((char *)m.data,"Liner notes"));
    reset(&m); assert(session_line(&s,"LANG ja",7,10)); assert(strstr((char *)m.data,"表示言語"));
    assert(session_line(&s,"WAIT 100",8,20)); assert(!session_ready(&s));
    assert(!session_line(&s,"PING",4,21)); session_tick(&s,119); assert(!session_ready(&s));
    session_tick(&s,120); assert(session_ready(&s));
    BoardResult result={0}; result.session=76; result.slot=2; result.selected=123;
    session_result(&s,&result,121); assert(s.current==0);
    reset(&m); assert(session_line(&s,"PING",4,122)); assert(strstr((char *)m.data,"PONG"));
    m.fail=1; session_line(&s,"PING",4,123); assert(!s.active); session_dispose(&s);
    puts("PASS: transport-free session supports menus, language, timer, stale completion rejection and output failure");
    m.fail=0; reset(&m);
    session_init(&s,io,88,3,1,1,0,LANGUAGE_EN);
    s.user=2; s.user_epoch=4; s.editing=1; s.pending=1; s.wake=9999;
    strcpy(s.draft.body,"unsaved text"); s.undo_available=1; strcpy(s.undo_title,"old title"); strcpy(s.undo_body,"old body");
    session_revoke(&s);
    assert(s.closing && !s.user && !s.editing && !s.pending && !s.wake);
    assert(!s.undo_available);
    for(size_t i=0;i<sizeof(s.undo_title);i++) assert(!s.undo_title[i]);
    for(size_t i=0;i<sizeof(s.undo_body);i++) assert(!s.undo_body[i]);
    for(size_t i=0;i<sizeof(s.draft);i++) assert(((unsigned char *)&s.draft)[i]==0);
    memset(&result,0,sizeof(result)); result.session=88; result.slot=3; result.auth=2; result.user_epoch=4;
    strcpy(result.text,"LOGGED IN bob\r\n");
    session_result(&s,&result,125);
    assert(s.closing && !s.user && !session_ready(&s));
    assert(!strstr((char *)m.data,"LOGGED IN"));
    session_dispose(&s);
    puts("PASS: transport-free revocation clears drafts/timers and rejects delayed authentication completion");
    reset(&m); session_init(&s,io,99,4,1,1,0,LANGUAGE_EN); s.user=2; s.user_epoch=4;
    assert(session_line(&s,"PASSWD",6,2000) && session_secret(&s));
    assert(session_line(&s,"old-secret-123",14,2001));
    assert(session_line(&s,"new-secret-123",14,2002));
    assert(session_secret(&s)); session_revoke(&s);
    for(size_t i=0;i<sizeof(s.old_password);i++) assert(!s.old_password[i] && !s.new_password[i]);
    session_dispose(&s);
    reset(&m); session_init(&s,io,100,4,1,1,0,LANGUAGE_EN); s.user=2; s.user_epoch=4;
    memset(&result,0,sizeof(result)); result.session=100; result.slot=4;
    result.password_changed=1; result.account_user=2; result.account_epoch=5;
    strcpy(result.text,"PASSWORD CHANGED\r\n"); session_result(&s,&result,3000);
    assert(s.user==2 && s.user_epoch==5 && session_ready(&s)); session_dispose(&s);
    puts("PASS: password change is transport-independent; revocation clears secrets and success advances session epoch");
    reset(&m); session_init(&s,io,101,4,1,1,1,LANGUAGE_EN);
    session_dimensions(&s,20,5); assert(s.columns==20 && s.rows==5);
    session_line(&s,"PAGER ON",8,4000);
    memset(&result,0,sizeof(result)); result.session=101; result.slot=4;
    strcpy(result.text,"ARTICLE 1 PARENT 0 AUTHOR guest\r\nTITLE test\r\nBODY 21\r\none\r\ntwo\r\nthree\r\nfour\r\nEND\r\n");
    session_result(&s,&result,4001); assert(s.page[0]);
    session_revoke(&s); assert(!s.page[0] && !s.page_offset && s.closing);
    session_dispose(&s);
    puts("PASS: adapter-supplied dimensions control paging and revocation clears retained article text");
    reset(&m); assert(terminal_init(&term,TERMINAL_RAW,bytes,&m));
    terminal_set_binary(&term,1);
    for(int i=0;i<256;i++) { unsigned char decoded=0; assert(terminal_binary_receive(&term,(unsigned char)i,&decoded)==1); assert(decoded==i); }
    terminal_set_binary(&term,0);
    reset(&m); session_init(&s,io,102,4,1,1,0,LANGUAGE_EN);
    s.binary=1; s.binary_deadline=60000; session_tick(&s,59999); assert(s.active);
    session_tick(&s,60000); assert(!s.active); session_dispose(&s);
    reset(&m); session_init(&s,io,103,4,1,1,0,LANGUAGE_EN);
    s.binary=1; assert(!session_binary_byte(&s,'~',100));
    session_tick(&s,15099); assert(s.active); session_tick(&s,15100); assert(!s.active); session_dispose(&s);
    puts("PASS: all raw terminal bytes preserved; binary idle and partial-frame deadlines enforced");
    reset(&m);session_init(&s,io,104,4,1,1,1,LANGUAGE_EN);s.user=2;reset(&m);
    s.binary=1;s.binary_menu=1;s.ui.enabled=0;s.binary_deadline=60000;
    session_mail_arrived(&s,2);session_mail_arrived(&s,2);session_tick(&s,100);
    assert(m.size==0 && s.mail_arrivals==2);
    s.binary=0;s.ui.enabled=1;s.password_mode=1;session_tick(&s,101);assert(m.size==0);
    s.password_mode=0;s.editing=1;session_tick(&s,102);assert(m.size==0);
    s.editing=0;s.zmodem=1;session_tick(&s,102);assert(m.size==0 && !session_ready(&s));s.zmodem=0;strcpy(s.page,"pending page");session_tick(&s,103);assert(m.size==0);
    s.page[0]=0;s.pending=1;session_tick(&s,104);assert(m.size==0);
    s.pending=0;s.chatting=1;session_tick(&s,105);assert(m.size==0);
    s.chatting=0;session_mail_notify(&s,2,1,1);assert(!s.mail_arrivals && s.mail_notify_deny);
    session_mail_arrived(&s,2);assert(!s.mail_arrivals);
    session_mail_notify(&s,2,0,0);assert(s.mail_notify_deny);
    session_mail_notify(&s,3,0,2);assert(s.mail_notify_deny);
    session_mail_notify(&s,2,0,2);assert(!s.mail_notify_deny);
    session_mail_arrived(&s,2);session_mail_arrived(&s,2);session_tick(&s,106);assert(strstr((char *)m.data,"2 new mail message(s) arrived") && !s.mail_arrivals);
    reset(&m);session_tick(&s,107);assert(m.size==0);
    session_mail_arrived(&s,3);assert(!s.mail_arrivals);
    session_mail_arrived(&s,2);session_revoke(&s);assert(!s.mail_arrivals);session_dispose(&s);
    reset(&m);session_init(&s,io,105,4,1,1,0,LANGUAGE_EN);s.user=2;reset(&m);
    session_mail_arrived(&s,2);session_tick(&s,108);assert(m.size==0 && !s.mail_arrivals);session_dispose(&s);
    puts("PASS: deferred mail arrival preserves binary/password/editor/page/worker/chat states, aggregates once, isolates recipient/raw mode and clears on revocation");
    return 0;
}
