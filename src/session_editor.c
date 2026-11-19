#define _POSIX_C_SOURCE 200809L
#include "editor_core.h"
#include "batch_protocol.h"
#include "list_navigation_protocol.h"
#include "traversal_protocol.h"
#include "editor_commands.h"
#include "editor_view.h"
#include "classic_input.h"
#include "terminal_pager.h"
#include "connector_protocol.h"
#include "runtime_limits.h"
#include "app_limits.h"
#include "activity.h"
#include "vault.h"
#include "session_internal.h"
#include "members.h"
#include "catalog.h"
#include "store_limits.h"
#include "profile_store.h"
#include "menu.h"
#include "chat.h"
#include "date_filter.h"
#include "vault_bridge.h"
#include "transfer_line.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <openssl/crypto.h>

static EditorDocument editor_document(Session *s);
static void editor_error(Session *s,EditorOutcome result);
static int editor_view_write(void *context,const char *text);
static int edit_draft(Session *s,const char *line);
static int classic_editor(Session *s,const char *line,char *translated,size_t size);

int session_impl_editor_preinput(Session *s,char **input,char *scratch) {
    char *line=*input;
    if(rtbbs(s)->editor.editing && rtbbs(s)->conversation.memo_input) { rtbbs(s)->conversation.memo_input=0;if(!strcmp(line,"/CANCEL"))return 1;BoardRequest q={0};q.op='&';q.action='M';if(strlen(line)>80 || !session_impl_safe_text(line)) { session_impl_emit(s,"ERR memo\r\n");return 1; }strcpy(q.body,line);session_impl_submit(s,&q);rtbbs(s)->conversation.chat_pending=s->core.pending;return 1; }
    if(rtbbs(s)->editor.editing && rtbbs(s)->editor.editor_line && !rtbbs(s)->editor.editor_input && !strcmp(line,"\\")) { rtbbs(s)->conversation.memo_input=1;session_impl_queue_text(s,rtbbs(s)->presentation.ui.language==LANGUAGE_JA?"接続者メモ > ":"Connection memo > ");return 1; }
    if(rtbbs(s)->editor.editing && rtbbs(s)->editor.editor_line && !rtbbs(s)->editor.editor_input && !rtbbs(s)->editor.editor_confirm && (!strcmp(line,"W") || !strcmp(line,"T") || !strncmp(line,"T ",2) || !strcmp(line,"#"))) { snprintf(scratch,(INPUT_LINE_MAX+2),"/%s",line);line=scratch; }

    if(rtbbs(s)->editor.editing && (!strcmp(line,"/BUFFER") || !strcmp(line,"/TOBUFFER"))) { BoardRequest q={0};q.op=')';q.action=!strcmp(line,"/BUFFER")?'E':'C';q.scope=1;if(q.action=='C')strcpy(q.body,rtbbs(s)->editor.draft.body);session_impl_submit(s,&q);return 1; }
    *input=line;

    return 0;
}

int session_impl_editor_input(Session *s,char *line) {
    if(rtbbs(s)->editor.editing && (!strcmp(line,"/#") || !strcmp(line,"/STATUS"))) { session_impl_session_status(s);return 1; }
    if(rtbbs(s)->editor.editing && (!strcmp(line,"/ONLINE") || !strcmp(line,"/W") || !strcmp(line,"/STATUS") || !strcmp(line,"/#") || !strcmp(line,"/TALK") || !strncmp(line,"/TALK ",6) || !strncmp(line,"/T ",3) || !strcmp(line,"/T") || !strncmp(line,"/MEMO ",6) || !strcmp(line,"/CALL"))) {
        BoardRequest q={0};q.op='&';q.action=(!strncmp(line,"/T",2))?'V':!strncmp(line,"/MEMO ",6)?'M':!strcmp(line,"/CALL")?'C':'O';
        if(q.action=='V') { rtbbs(s)->conversation.talk_once=strncmp(line,"/TALK",5)!=0;rtbbs(s)->conversation.talk_return_editor=rtbbs(s)->conversation.talk_once; }
        if(q.action=='V' && !board_feature_enabled("chat")) { session_impl_emit(s,"ERR feature disabled\r\n");return 1; }
        const char *arg=strchr(line,' ');if(q.action=='V' && arg && !session_impl_talk_number(arg+1,&q.chat_target)) { session_impl_emit(s,"ERR command\r\n");return 1; }
        if(q.action=='M') { if(strlen(arg+1)>80 || !session_impl_safe_text(arg+1)) { session_impl_emit(s,"ERR command\r\n");return 1; }strcpy(q.body,arg+1); }
        if(q.action=='C')strcpy(q.body,"SYSOP");
        session_impl_submit(s,&q);rtbbs(s)->conversation.chat_pending=s->core.pending;return 1;
    }
    if(rtbbs(s)->editor.editing) {
        char translated[INPUT_LINE_MAX+128]="";if(classic_editor(s,line,translated,sizeof(translated)))return 1;
        if(*translated)line=translated;
        if(!strcmp(line,"/HELP")) { session_impl_show_help(s,rtbbs(s)->editor.draft.op=='m'?"MAIL":"POST"); return 1; }
        if(!strcmp(line,"/SAVE") || ((rtbbs(s)->editor.draft.op=='Y' || rtbbs(s)->editor.draft.op==')' || rtbbs(s)->editor.draft.op=='(') && !strcmp(line,"."))) {
            if(rtbbs(s)->editor.draft.op=='Y') { if(strlen(rtbbs(s)->editor.draft.body)>8192) { session_impl_emit(s,"ERR profile text\r\n");return 1; }BoardRequest q=rtbbs(s)->editor.draft;snprintf(q.body,sizeof(q.body),"%.48s|%.8192s",rtbbs(s)->editor.draft.title,rtbbs(s)->editor.draft.body);session_impl_submit(s,&q);return 1; }
            if(rtbbs(s)->editor.draft.op=='(' || rtbbs(s)->editor.draft.op==')') { session_impl_submit(s,&rtbbs(s)->editor.draft);return 1; }
            if(rtbbs(s)->editor.draft.op=='?') { session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,MSG_SYSOP_MAIL_NO_DRAFT));return 1; }
            if(rtbbs(s)->editor.draft.op=='m') { rtbbs(s)->editor.draft.action='W';rtbbs(s)->editor.draft.mail_revision=rtbbs(s)->mail_state.mail_revision;session_impl_mail_submit(s,&rtbbs(s)->editor.draft);return 1; }
            if(!s->core.user) { session_impl_emit(s,"ERR login required\r\n"); return 1; }
            rtbbs(s)->editor.draft.op='w'; rtbbs(s)->editor.draft.cursor=rtbbs(s)->editor.draft_revision; session_impl_submit(s,&rtbbs(s)->editor.draft); return 1;
        }
        if(rtbbs(s)->editor.draft.op=='m' && (!strncmp(line,"/ATTACH ",8) || !strcmp(line,"/DETACH") || !strncmp(line,"/DETACH ",8))) {
            uint32_t id=0;const char *tail;
            if(strcmp(line,"/DETACH") && (!session_impl_number_arg(line+8,&id,&tail) || *tail || id>VAULT_ID_MAX)) { session_impl_emit(s,"ERR vault syntax\r\n");return 1; }
            BoardRequest q={0};q.action='A';q.number=id;q.cursor=!strncmp(line,"/DETACH ",8);q.mail_attachments=rtbbs(s)->editor.draft.mail_attachments;session_impl_mail_submit(s,&q);return 1;
        }
        if(edit_draft(s,line)) return 1;
        if(!strcmp(line,"/CANCEL")) { rtbbs(s)->editor.editing=0;rtbbs(s)->editor.editor_upload=rtbbs(s)->editor.editor_confirm=rtbbs(s)->editor.editor_upload_question=0; memset(&rtbbs(s)->editor.draft,0,sizeof(rtbbs(s)->editor.draft)); rtbbs(s)->editor.draft_revision=0; session_impl_undo_clear(s); session_impl_emit(s,"CANCELLED\r\n"); }
        else if(!strcmp(line,"/PREVIEW")) {
            if(rtbbs(s)->editor.draft.op=='m')for(unsigned i=0;i<rtbbs(s)->editor.draft.mail_attachments.count;i++) { char text[256];snprintf(text,sizeof(text),rtbbs(s)->presentation.ui.enabled?ui_text(&rtbbs(s)->presentation.ui,MSG_MAIL_ATTACHMENT_SET):"MAIL ATTACHMENT %u\r\n",rtbbs(s)->editor.draft.mail_attachments.ids[i]);session_impl_queue_text(s,text); }
            char preview[BOARD_REPLY];
            snprintf(preview,sizeof(preview),"PREVIEW PARENT %u AUTHOR %s\r\nTITLE %s\r\nBODY %zu\r\n%sEND\r\n",
                     rtbbs(s)->editor.draft.number,s->core.user?s->core.username:"guest",rtbbs(s)->editor.draft.title,strlen(rtbbs(s)->editor.draft.body),rtbbs(s)->editor.draft.body);
            session_impl_emit(s,preview);
        }
        else if(!strcmp(line,".")) {
            if(!rtbbs(s)->editor.draft.body[0] && rtbbs(s)->editor.draft.op!='(' && rtbbs(s)->editor.draft.op!=')') { session_impl_emit(s,"ERR empty body\r\n"); return 1; }
            if(rtbbs(s)->editor.draft.op=='(' || rtbbs(s)->editor.draft.op==')')session_impl_submit(s,&rtbbs(s)->editor.draft);
            else if(rtbbs(s)->editor.draft.op=='m') { rtbbs(s)->editor.draft.action='S';rtbbs(s)->editor.draft.mail_revision=rtbbs(s)->mail_state.mail_revision;session_impl_mail_submit(s,&rtbbs(s)->editor.draft); }
            else if(rtbbs(s)->editor.draft.op=='?') { session_impl_submit(s,&rtbbs(s)->editor.draft);sysop_mail_submitted(&rtbbs(s)->conversation.sysop_mail,s->core.pending); }
            else { rtbbs(s)->editor.draft.op='P'; session_impl_submit(s,&rtbbs(s)->editor.draft); }
            if(s->core.pending) { rtbbs(s)->editor.editing=0;rtbbs(s)->editor.editor_upload=rtbbs(s)->editor.editor_confirm=rtbbs(s)->editor.editor_upload_question=0; session_impl_undo_clear(s); }
        } else {
            if(!strncmp(line,"..",2) || !strcmp(line,"//PREVIEW") || !strcmp(line,"//LINES") || !strcmp(line,"//UNDO") || !strcmp(line,"//SAVE") || !strcmp(line,"//HELP") ||
               (line[0]=='/' && (editor_command_word(line+1,"/TITLE") || editor_command_word(line+1,"/REPLACE") || editor_command_word(line+1,"/DELETE") || editor_command_word(line+1,"/INSERT")))) line++;
            EditorOutcome result=session_impl_edit_apply(s,EDITOR_APPEND_LINE,0,0,line);
            if(result!=EDITOR_OK)editor_error(s,result);
        }
        return 1;
    }

    return 0;
}

int session_impl_editor_mode_command(Session *s,char *line) {
    if(!strcmp(line,"EDITOR LINE") || !strcmp(line,"EDITOR BETA")) { rtbbs(s)->editor.editor_line=!strcmp(line,"EDITOR LINE");rtbbs(s)->editor.editor_input=0;rtbbs(s)->editor.editor_at=1;session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,rtbbs(s)->editor.editor_line?MSG_EDITOR_LINE:MSG_EDITOR_BETA));return 1; }

    return 0;
}

int session_impl_draft_command(Session *s,char *line) {
    if(!strcmp(line,"DRAFT") || !strcmp(line,"RESUME") || !strncmp(line,"DRAFT DELETE ",13)) {
        if(!s->core.user) { session_impl_emit(s,"ERR login required\r\n"); return 1; }
        BoardRequest q={0}; q.op=!strcmp(line,"DRAFT")?'d':!strcmp(line,"RESUME")?'o':'X';
        if(q.op=='X' && !session_impl_audit_number(line+13,&q.cursor)) { session_impl_emit(s,"ERR draft revision\r\n"); return 1; }
        session_impl_submit(s,&q); return 1;
    }

    return 0;
}

int session_impl_buffer_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(completion->service_action=='8') { memset(&rtbbs(s)->editor.draft,0,sizeof(rtbbs(s)->editor.draft));session_impl_undo_clear(s);rtbbs(s)->editor.draft.op=')';rtbbs(s)->editor.draft.action=completion->service_number==1?'B':'C';strcpy(rtbbs(s)->editor.draft.body,completion->draft_body);rtbbs(s)->editor.editing=1;rtbbs(s)->editor.editor_input=0;rtbbs(s)->editor.editor_at=1; }
    if(completion->service_action=='9') { if(!rtbbs(s)->editor.editing || session_impl_edit_apply(s,EDITOR_APPEND_BLOCK,0,0,completion->draft_body)!=EDITOR_OK)session_impl_emit(s,"ERR body limit 262144 bytes; cancel or finish\r\n");else session_impl_emit(s,"BUFFER IMPORTED\r\n");session_impl_prompt(s);return 1; }
    if(completion->service_action=='0' && completion->service_number) { rtbbs(s)->editor.editing=0;rtbbs(s)->editor.editor_upload=rtbbs(s)->editor.editor_confirm=rtbbs(s)->editor.editor_upload_question=0;memset(&rtbbs(s)->editor.draft,0,sizeof(rtbbs(s)->editor.draft));session_impl_undo_clear(s); }

    return 0;
}

int session_impl_board_editor_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(completion->service_action=='4' || completion->service_action=='6') {
        memset(&rtbbs(s)->editor.draft,0,sizeof(rtbbs(s)->editor.draft));rtbbs(s)->editor.draft_revision=0;session_impl_undo_clear(s);
        rtbbs(s)->editor.draft.op=completion->service_action=='4'?'(':'P';rtbbs(s)->editor.draft.action='W';rtbbs(s)->editor.draft.number=completion->service_action=='4'?completion->service_number:0;
        strcpy(rtbbs(s)->editor.draft.body,completion->draft_body);strcpy(rtbbs(s)->editor.draft.title,completion->draft_title);rtbbs(s)->editor.editing=1;rtbbs(s)->editor.editor_input=0;rtbbs(s)->editor.editor_at=1;
    }
    if(completion->service_action=='5') { rtbbs(s)->editor.editing=!completion->service_number;if(!rtbbs(s)->editor.editing) { memset(&rtbbs(s)->editor.draft,0,sizeof(rtbbs(s)->editor.draft));session_impl_undo_clear(s); } }

    return 0;
}

static EditorDocument editor_document(Session *s) {
    return (EditorDocument){rtbbs(s)->editor.draft.title,rtbbs(s)->editor.draft.body,rtbbs(s)->editor.undo_title,rtbbs(s)->editor.undo_body,&rtbbs(s)->editor.undo_available};
}

void session_impl_undo_clear(Session *s) { EditorDocument d=editor_document(s);editor_discard_undo(&d); }

EditorOutcome session_impl_edit_apply(Session *s,EditorOperation op,uint32_t first,uint32_t last,const char *text) {
    EditorDocument d=editor_document(s);return editor_apply(&d,op,first,last,text);
}

static void editor_error(Session *s,EditorOutcome result) {
    session_impl_emit(s,result==EDITOR_NO_UNDO?"ERR no undo\r\n":result==EDITOR_TITLE_INVALID?"ERR title 1..80 bytes, no controls\r\n":
           result==EDITOR_CONTROL?"ERR control character\r\n":result==EDITOR_LIMIT?"ERR body limit 262144 bytes; cancel or finish\r\n":"ERR draft line\r\n");
}

static int editor_view_write(void *context,const char *text) {
    Session *s=context;session_impl_queue_text(s,text);return s->core.active;
}

static int edit_draft(Session *s,const char *line) {
    DraftCommand session_impl_command=editor_draft_command(line);
    if(session_impl_command.kind==DRAFT_UNHANDLED)return 0;
    if(session_impl_command.kind==DRAFT_INVALID) { session_impl_emit(s,"ERR draft line\r\n");return 1; }
    EditorDocument d=editor_document(s);
    if(session_impl_command.kind==DRAFT_LINES) {
        char *out=malloc(CONTENT_DISPLAY);
        if(!out) { session_impl_emit(s,"ERR board busy\r\n");return 1; }
        if(editor_view_draft(&d,out,CONTENT_DISPLAY)==EDITOR_VIEW_OK)session_impl_emit(s,out);
        else editor_error(s,EDITOR_INVALID);
        free(out);return 1;
    }
    EditorOutcome result=editor_apply(&d,session_impl_command.operation,session_impl_command.line,session_impl_command.line,session_impl_command.text);
    if(result!=EDITOR_OK)editor_error(s,result);
    else session_impl_emit(s,session_impl_command.operation==EDITOR_UNDO?"DRAFT UNDONE\r\n":session_impl_command.operation==EDITOR_SET_TITLE?"DRAFT TITLE CHANGED\r\n":
        session_impl_command.operation==EDITOR_INSERT?"DRAFT LINE INSERTED\r\n":session_impl_command.operation==EDITOR_REPLACE?"DRAFT LINE REPLACED\r\n":"DRAFT LINE DELETED\r\n");
    return 1;
}

unsigned session_impl_editor_count(const Session *s) {
    EditorDocument d={(char *)rtbbs_const(s)->editor.draft.title,(char *)rtbbs_const(s)->editor.draft.body,(char *)rtbbs_const(s)->editor.undo_title,(char *)rtbbs_const(s)->editor.undo_body,(int *)&rtbbs_const(s)->editor.undo_available};
    EditorInfo info;return editor_inspect(&d,&info)==EDITOR_OK?info.lines:0;
}

static int classic_editor(Session *s,const char *line,char *translated,size_t size) {
    if(rtbbs(s)->editor.editor_upload_question) {
        if(!*line || !strcmp(line,"Y") || !strcmp(line,"y")) { session_impl_edit_apply(s,EDITOR_CLEAR,0,0,NULL); }
        else if(strcmp(line,"N") && strcmp(line,"n")) { session_impl_queue_text(s,"Y / N > ");return 1; }
        rtbbs(s)->editor.editor_upload_question=0;rtbbs(s)->editor.editor_at=session_impl_editor_count(s)+1;rtbbs(s)->editor.editor_input=1;rtbbs(s)->editor.editor_upload=1;rtbbs(s)->editor.editor_deadline=s->core.now+3000;
        session_impl_queue_text(s,rtbbs(s)->presentation.ui.language==LANGUAGE_JA?"テキスト受信中。3秒間の無入力で終了します。\r\n":"Receiving text; 3 seconds idle ends input.\r\n");return 1;
    }
    if(rtbbs(s)->editor.editor_confirm) { char c=*line;if(c>='a' && c<='z')c-=32;if(c && !line[1] && (c=='W' || c=='E' || c=='Q' || c=='0')) { rtbbs(s)->editor.editor_confirm=0;if(c=='E')return 1;snprintf(translated,size,"%s",c=='W'?".":"/CANCEL");return 0; }session_impl_queue_text(s,"W / E / 0 > ");return 1; }
    if(!strcmp(line,"/E") || !strcmp(line,"/EDITOR LINE")) {
        rtbbs(s)->editor.editor_line=1;rtbbs(s)->editor.editor_input=0;rtbbs(s)->editor.editor_at=1;session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,MSG_EDITOR_LINE));return 1;
    }
    if(!strcmp(line,"/EDITOR BETA")) { rtbbs(s)->editor.editor_line=0;rtbbs(s)->editor.editor_input=0;session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,MSG_EDITOR_BETA));return 1; }
    if(!strcmp(line,"/?") || (rtbbs(s)->editor.editor_line && !rtbbs(s)->editor.editor_input && !strcmp(line,"?"))) { session_impl_queue_text(s,help_text(rtbbs(s)->presentation.ui.language,"EDITOR")?help_text(rtbbs(s)->presentation.ui.language,"EDITOR"):ui_text(&rtbbs(s)->presentation.ui,MSG_EDITOR_HELP));return 1; }
    if(!strcmp(line,"/M") || (rtbbs(s)->editor.editor_line && !rtbbs(s)->editor.editor_input && !strcmp(line,"M"))) { rtbbs(s)->editor.editor_numbers=!rtbbs(s)->editor.editor_numbers;session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,MSG_EDITOR_NUMBERS));session_impl_queue_text(s,rtbbs(s)->editor.editor_numbers?"ON\r\n":"OFF\r\n");return 1; }
    if(!strcmp(line,"/N")) { session_impl_edit_apply(s,EDITOR_CLEAR,0,0,NULL);rtbbs(s)->editor.editor_at=1;return 1; }
    if(!rtbbs(s)->editor.editor_line) { if(!strcmp(line,"//")) {
        if(rtbbs(s)->presentation.ui.enabled) { rtbbs(s)->editor.editor_confirm=1;session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,MSG_CLASSIC_WRITE));return 1; }
        snprintf(translated,size,".");
    }return 0; }
    if(rtbbs(s)->editor.editor_input==4) { rtbbs(s)->editor.editor_input=0;if(!*line)return 1;char search[INPUT_LINE_MAX+128];snprintf(search,sizeof(search),"S%u-%u %s",rtbbs(s)->reading.search_first,rtbbs(s)->reading.search_last,line);return classic_editor(s,search,translated,size); }
    if(rtbbs(s)->editor.editor_input) {
        if(rtbbs(s)->editor.editor_upload)rtbbs(s)->editor.editor_deadline=s->core.now+3000;
        if(!strcmp(line,"//")) { rtbbs(s)->editor.editor_input=0;rtbbs(s)->editor.editor_upload=0;return 1; }
        if(*line=='/' && (editor_command_word(line,"/SAVE") || editor_command_word(line,"/CANCEL") || editor_command_word(line,"/UNDO") || editor_command_word(line,"/PREVIEW") || editor_command_word(line,"/ATTACH") || editor_command_word(line,"/DETACH") || editor_command_word(line,"/TITLE") || editor_command_word(line,"/LINES") || editor_command_word(line,"/REPLACE") || editor_command_word(line,"/INSERT") || editor_command_word(line,"/DELETE") || editor_command_word(line,"/HELP")))return 0;
        unsigned count=session_impl_editor_count(s);if(rtbbs(s)->editor.editor_at>count+1)rtbbs(s)->editor.editor_at=count+1;
        EditorOperation op=rtbbs(s)->editor.editor_input==2 && rtbbs(s)->editor.editor_at<=count?EDITOR_REPLACE:EDITOR_INSERT;
        EditorOutcome result=session_impl_edit_apply(s,op,rtbbs(s)->editor.editor_at,rtbbs(s)->editor.editor_at,line);
        if(result==EDITOR_OK) {
            session_impl_emit(s,op==EDITOR_REPLACE?"DRAFT LINE REPLACED\r\n":"DRAFT LINE INSERTED\r\n");
            rtbbs(s)->editor.editor_at++;if(rtbbs(s)->editor.editor_input==2)rtbbs(s)->editor.editor_input=0;
        } else session_impl_emit(s,"ERR body limit 262144 bytes; cancel or finish\r\n");
        return 1;
    }
    if(*line=='/' || !strcmp(line,"."))return 0;
    if(!strcmp(line,"0")) { rtbbs(s)->editor.editor_confirm=1;session_impl_queue_text(s,rtbbs(s)->presentation.ui.language==LANGUAGE_JA?"W: 書き込み / E: 編集へ戻る / 0: 取り消し > ":"W: write / E: edit / 0: cancel > ");return 1; }
    unsigned count=session_impl_editor_count(s);if(!rtbbs(s)->editor.editor_at || rtbbs(s)->editor.editor_at>count+1)rtbbs(s)->editor.editor_at=1;
    LineCommand request=editor_line_command(line,count,rtbbs(s)->editor.editor_at);
    if(request.kind==LINE_INVALID)goto invalid;
    if(request.kind==LINE_SELECT) { rtbbs(s)->editor.editor_at=request.first;return 1; }
    if(request.kind==LINE_NEW) { session_impl_edit_apply(s,EDITOR_CLEAR,0,0,NULL);rtbbs(s)->editor.editor_at=1;rtbbs(s)->editor.editor_input=1;return 1; }
    if(request.kind==LINE_UPLOAD && count) { rtbbs(s)->editor.editor_upload_question=1;session_impl_queue_text(s,rtbbs(s)->presentation.ui.language==LANGUAGE_JA?"バッファをクリアしますか？ [Y]/N（Nは追記） > ":"Clear buffer? [Y]/N (N appends) > ");return 1; }
    if(request.kind==LINE_APPEND || request.kind==LINE_UPLOAD) { rtbbs(s)->editor.editor_at=count+1;rtbbs(s)->editor.editor_input=1;if(request.kind==LINE_UPLOAD) { rtbbs(s)->editor.editor_upload=1;rtbbs(s)->editor.editor_deadline=s->core.now+3000;session_impl_queue_text(s,rtbbs(s)->presentation.ui.language==LANGUAGE_JA?"テキスト受信中。3秒間の無入力で編集へ戻ります。\r\n":"Receiving text; 3 seconds idle returns to editor.\r\n"); }return 1; }
    if(request.kind==LINE_SEARCH) {
        uint32_t first=request.first,last=request.last;const char *query=request.query;
        if(!*query) { rtbbs(s)->reading.search_first=first;rtbbs(s)->reading.search_last=last;rtbbs(s)->editor.editor_input=4;session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,MSG_EDITOR_SEARCH));return 1; }
        unsigned matches=0;EditorDocument document=editor_document(s);
        EditorViewResult result=editor_view_search(&document,first,last,query,rtbbs(s)->editor.editor_numbers,rtbbs(s)->presentation.ui.enabled,editor_view_write,s,&matches);
        if(result==EDITOR_VIEW_MEMORY) { session_impl_emit(s,"ERR board busy\r\n");return 1; }
        if(result==EDITOR_VIEW_OUTPUT)return 1;
        if(result!=EDITOR_VIEW_OK)goto invalid;
        char total[80];snprintf(total,sizeof(total),rtbbs(s)->presentation.ui.language==LANGUAGE_JA?"一致: %u 件\r\n":"Matches: %u\r\n",matches);session_impl_queue_text(s,total);return 1;
    }
    if(request.kind==LINE_HELP) { session_impl_queue_text(s,help_text(rtbbs(s)->presentation.ui.language,"EDITOR")?help_text(rtbbs(s)->presentation.ui.language,"EDITOR"):ui_text(&rtbbs(s)->presentation.ui,MSG_EDITOR_HELP));return 1; }
    uint32_t first=request.first,last=request.last;
    if(request.kind==LINE_LIST && !count) { edit_draft(s,"/LINES");return 1; }
    if(request.kind==LINE_EDIT || request.kind==LINE_INSERT) {
        rtbbs(s)->editor.editor_at=first;rtbbs(s)->editor.editor_input=request.kind==LINE_EDIT?2:3;
        if(request.kind==LINE_EDIT && rtbbs(s)->presentation.ui.enabled) {
            EditorDocument document=editor_document(s);EditorSpan span;
            if(editor_range(&document,first,first,&span)!=EDITOR_OK)goto invalid;
            char old[INPUT_LINE_MAX+80];
            snprintf(old,sizeof(old),"%u: %.*s\r\n",first,(int)(span.size-2),span.text);session_impl_queue_text(s,old);
        }
        return 1;
    }
    if(request.kind==LINE_LIST) {
        EditorDocument document=editor_document(s);char *out=malloc(CONTENT_DISPLAY);
        if(!out) { session_impl_emit(s,"ERR board busy\r\n");return 1; }
        EditorViewResult result=editor_view_lines(&document,first,last,rtbbs(s)->editor.editor_numbers,out,CONTENT_DISPLAY);
        if(result==EDITOR_VIEW_OK)session_impl_queue_text(s,out);
        free(out);if(result!=EDITOR_VIEW_OK)goto invalid;
        return 1;
    }
    if(request.kind==LINE_DELETE || request.kind==LINE_QUOTE) {
        EditorOutcome result=session_impl_edit_apply(s,request.kind==LINE_DELETE?EDITOR_DELETE:EDITOR_QUOTE,first,last,NULL);
        if(result!=EDITOR_OK)goto invalid;
        if(request.kind==LINE_DELETE)rtbbs(s)->editor.editor_at=first;
        return 1;
    }
invalid:session_impl_emit(s,"ERR draft line\r\n");return 1;
}
