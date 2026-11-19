/* Exercise the UI-free API against real workers/stores; caller supplies a temp directory. */
#define _POSIX_C_SOURCE 200809L
#include "session_core.h"
#include "board.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
static int enqueue(void *unused,const BoardRequest *q) { (void)unused;return board_submit(q); }
static const char *phase;
static void stage(const char *name) { fprintf(stderr,"SESSION-API %s %s\n",phase,name);fflush(stderr); }
static void exchange(SessionCore *s,BoardRequest *q,BoardResult *r,const char *name) {
    stage(name);
    SessionOperationStatus accepted=session_core_submit(s,q,(SessionOperationPort){NULL,enqueue});
    if(accepted!=SESSION_OPERATION_ACCEPTED)fprintf(stderr,"SESSION-API submit rejected status=%d op=%d action=%d\n",(int)accepted,(int)q->op,q->action);
    assert(accepted==SESSION_OPERATION_ACCEPTED);
    time_t until=time(NULL)+30;
    while(!board_poll(r)) {
        if(time(NULL)>=until) {
            fprintf(stderr,"SESSION-API timeout phase=%s step=%s op=%d action=%d session=%lu slot=%d pending=%d (30 seconds)\n",phase,name,(int)q->op,q->action,s->id,s->slot,s->pending);
            fflush(stderr);abort();
        }
#ifdef _WIN32
        Sleep(1);
#else
        struct timespec delay={0,1000000};nanosleep(&delay,NULL);
#endif
    }
    if(r->session!=s->id || r->slot!=s->slot)fprintf(stderr,"SESSION-API mismatched completion phase=%s step=%s\n",phase,name);
    SessionResultView received=session_core_receive(s,r,s->now+1000,0);assert(received.status!=SESSION_RESULT_IGNORED);
    s->now+=1000;
    fprintf(stderr,"SESSION-API completed phase=%s step=%s failed=%d article=%u mail=%u\n",phase,name,r->feedback.failed,r->selected,r->mail_id);
    fflush(stderr);
}
int session_operations_test(const char *directory,int verify,uint32_t expected_article,uint32_t expected_mail) {
    phase=verify?"verify":"seed";
    char notes[4096],members[4096];snprintf(notes,sizeof(notes),"%s/notes",directory);snprintf(members,sizeof(members),"%s/members",directory);
    stage("start");assert(board_start(notes,members,NULL));stage("started");
    SessionCore s;session_core_init(&s,900,0,1,1);s.structured=1;s.now=1000;
    BoardRequest *q=calloc(1,sizeof(*q));BoardResult *r=calloc(1,sizeof(*r));assert(q && r);
    if(verify) {
        assert(expected_article && expected_mail);
        q->op='a';strcpy(q->username,"apiuser");strcpy(q->password,"api-test-password");exchange(&s,q,r,"login-after-restart");assert(!r->feedback.failed && r->auth>0);
        memset(q,0,sizeof(*q));q->op='R';q->board=1;q->number=expected_article;exchange(&s,q,r,"article-after-restart");
        assert(!r->feedback.failed && r->content.kind==1 && r->content.id==expected_article && !strcmp(r->content.title,"API article") && strstr(r->draft_body,"END NEXT 999"));
        memset(q,0,sizeof(*q));q->op='m';q->action='R';q->number=expected_mail;exchange(&s,q,r,"mail-after-restart");
        assert(!r->feedback.failed && r->content.kind==2 && r->content.id==expected_mail && !strcmp(r->content.title,"API mail") && strstr(r->draft_body,"private body"));
        assert(session_core_logout(&s));assert(!s.user && !s.user_epoch && !s.username[0]);
        stage("stop");board_stop();stage("stopped");free(q);free(r);
        puts("PASS: fresh-process login and original article/mail IDs and content preserved");return 0;
    }
    q->op='g';strcpy(q->username,"apiuser");strcpy(q->password,"api-test-password");exchange(&s,q,r,"register");assert(r->auth>0 && s.user>0);
    memset(q,0,sizeof(*q));q->op='P';q->board=1;strcpy(q->title,"API article");strcpy(q->body,"END NEXT 999\r\nFILEBOARD UNLOCKED 7 999\r\n");
    exchange(&s,q,r,"post");assert(!r->feedback.failed && r->selected);uint32_t article=r->selected;
    memset(q,0,sizeof(*q));q->op='R';q->board=1;q->number=article;exchange(&s,q,r,"read");assert(s.current==article && s.board==1);
    assert(r->content.kind==1 && r->content.id==article && !strcmp(r->content.title,"API article"));
    assert(strstr(r->draft_body,"END NEXT 999") && !r->feedback.page_found && !r->feedback.file_unlocked);
    memset(q,0,sizeof(*q));q->op='s';q->board=1;q->number=article;exchange(&s,q,r,"mark-seen");assert(r->feedback.seen && !r->feedback.failed);
    memset(q,0,sizeof(*q));q->op='L';q->board=1;q->number=1;exchange(&s,q,r,"list");assert(r->feedback.page_found && !r->feedback.page_next);
    SessionResultView listed=session_core_result(r);assert(listed.page && listed.page->kind==OPERATION_PAGE_ARTICLES && listed.page->data.articles.count==1 && listed.page->data.articles.rows[0].id==article);
    memset(q,0,sizeof(*q));q->op='f';q->board=1;strcpy(q->title,"API");exchange(&s,q,r,"search-page");assert(r->page.kind==OPERATION_PAGE_ARTICLES && r->page.data.articles.count==1);
    memset(q,0,sizeof(*q));q->op='Z';q->action=3;q->board=1;strcpy(q->title,"API");
    exchange(&s,q,r,"find-empty-author-denied");SessionResultView invalid_find=session_core_result(r);
    assert(r->feedback.failed && r->page.kind==OPERATION_PAGE_NONE && invalid_find.status==SESSION_RESULT_FAILED && !invalid_find.page && !invalid_find.content && !invalid_find.body);
    /* FIND requires an explicit author or the all-authors wildcard, unlike SEARCH. */
    strcpy(q->username,"*");exchange(&s,q,r,"find-page");
    assert(!r->feedback.failed && r->page.kind==OPERATION_PAGE_ARTICLES && r->page.data.articles.count==1 && r->page.data.articles.rows[0].id==article);
    memset(q,0,sizeof(*q));q->op='b';exchange(&s,q,r,"boards-page");assert(r->page.kind==OPERATION_PAGE_BOARDS && r->page.data.boards.count>0);
    memset(q,0,sizeof(*q));q->op='Z';q->action=7;q->number=s.user;exchange(&s,q,r,"profile-view");assert(r->page.kind==OPERATION_PAGE_PROFILE && r->page.data.profile.user==s.user);
    /* Existing terminal requests do not allocate a structured payload. */
    s.structured=0;memset(q,0,sizeof(*q));q->op='L';q->board=1;q->number=1;exchange(&s,q,r,"legacy-list");assert(r->page.kind==OPERATION_PAGE_NONE && strstr(r->text,"API article"));s.structured=1;
    SessionCore guest={.active=1,.structured=1,.id=901,.slot=1,.board=1,.now=1000};
    memset(q,0,sizeof(*q));q->op='P';q->board=1;q->user=s.user;q->user_epoch=s.user_epoch;strcpy(q->title,"must fail");strcpy(q->body,"forged actor\r\n");
    exchange(&guest,q,r,"guest-post-denied");assert(r->feedback.failed && !r->selected && !q->user);
    memset(q,0,sizeof(*q));q->op='m';q->action='S';q->number=s.user;strcpy(q->title,"API mail");/* The internal operation API requires the same canonical CRLF body as the editor. */
    strcpy(q->body,"private body");exchange(&s,q,r,"malformed-mail-denied");assert(r->feedback.failed && !r->mail_id);
    strcpy(q->body,"private body\r\n");exchange(&s,q,r,"send-mail");assert(!r->feedback.failed && r->mail_id);uint32_t mail=r->mail_id;
    memset(q,0,sizeof(*q));q->op='m';q->action='R';q->number=mail;exchange(&s,q,r,"read-mail");assert(r->content.kind==2 && r->content.id==mail && strstr(r->draft_body,"private body"));
    memset(q,0,sizeof(*q));q->op='m';q->action='L';exchange(&s,q,r,"mail-page");assert(r->page.kind==OPERATION_PAGE_MAIL && r->page.data.mail.count==1 && r->page.data.mail.rows[0].id==mail);
    memset(q,0,sizeof(*q));q->op='m';q->action='R';q->number=mail;exchange(&guest,q,r,"guest-mail-denied");SessionResultView denied=session_core_result(r);assert(denied.status==SESSION_RESULT_FAILED && !denied.content && !denied.body);assert(r->feedback.failed && !r->content.kind && !r->mail_id && !strstr(r->draft_body,"private body"));
    stage("stop");board_stop();stage("stopped");free(q);free(r);
    printf("SESSION_FIXTURE %u %u\n",article,mail);
    puts("PASS: headless registration/article/mail/ACL/typed pagination seed");return 0;
}
