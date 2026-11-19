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
#include "session_policy_core.h"
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

int session_impl_transfer_command(Session *s,char *line) {
    if(!strcmp(line,"BATCH") || !strncmp(line,"BATCH ",6)) {
        if(!s->user) { session_impl_emit(s,"ERR login required\r\n");return 1; }
        if(!board_feature_enabled("vault")) { session_impl_emit(s,"ERR feature disabled\r\n");return 1; }
        if(!strncmp(line,"BATCH MATCH ",12)) { if(!line[12] || strlen(line+12)>80 || !session_impl_safe_text(line+12))goto batch_error;BoardRequest q={0};q.op='Y';q.action=VAULT_CMD_MATCH;snprintf(q.body,sizeof(q.body),"%s|%s",vault_navigation_path(&s->vault),line+12);session_impl_submit(s,&q);return 1; }
        if(!strncmp(line,"BATCH COPY ",11)) {
            const char *path=line+11;if(!*path || strlen(path)>48 || !session_impl_safe_text(path) || !s->batch.count)goto batch_error;
            uint32_t next;int state=batch_copy_begin(&s->batch,path,&next);
            if(state<0)goto batch_error;
            if(!state) { session_impl_emit(s,"BATCH COPY COMPLETE\r\n");return 1; }
            BoardRequest q={0};q.op='Y';q.action=VAULT_CMD_COPY;q.number=next;strcpy(q.body,path);session_impl_submit(s,&q);s->batch_copying=s->pending;return 1;
        }
        if(!strcmp(line,"BATCH CLEAR"))batch_reset(&s->batch);
        else if(!strncmp(line,"BATCH ADD ",10) || !strncmp(line,"BATCH REMOVE ",13)) {
            int add=!strncmp(line,"BATCH ADD ",10);const char *tail;uint32_t id;
            if(!session_impl_number_arg(line+(add?10:13),&id,&tail) || *tail || id>VAULT_ID_MAX)goto batch_error;
            if(add) { if(!batch_add(&s->batch,id,ln_limit("BATCH_FILES",BATCH_CAPACITY)))goto batch_error; }
            else batch_remove(&s->batch,id);
        } else if(!strcmp(line,"BATCH SEND")) {
            if(!s->batch.count)goto batch_error;
            if(!s->zmodem_available || !s->binary_allowed) { session_impl_queue_text(s,ui_text(&s->ui,MSG_ZMODEM_UNAVAILABLE));return 1; }
            BatchIds remaining=batch_ids(&s->batch,1);unsigned pending=remaining.count;
            if(!batch_format_ids(&remaining,s->zmodem_text,sizeof(s->zmodem_text)))goto batch_error;
            if(!pending) { session_impl_emit(s,"BATCH COMPLETE\r\n");return 1; }
            s->zmodem_protocol=s->transfer_protocol;if(s->zmodem_protocol==1 || s->zmodem_protocol>=5)goto batch_error;
            s->zmodem_receive=2;s->zmodem_id=pending;s->zmodem_start=1;
            BoardRequest q={0};q.op='@';session_impl_submit(s,&q);if(!s->pending)s->zmodem_start=0;return 1;
        } else if(strcmp(line,"BATCH") && strcmp(line,"BATCH LIST"))goto batch_error;
        char text[384],ids[192];BatchIds selected=batch_ids(&s->batch,0);
        if(!batch_format_ids(&selected,ids,sizeof(ids)))goto batch_error;
        snprintf(text,sizeof(text),s->ui.enabled?ui_text(&s->ui,MSG_BATCH_LIST):"BATCH %u %s\r\n",s->batch.count,*ids?ids:"-");session_impl_queue_text(s,text);
        for(unsigned i=0;i<s->batch.count;i++) { snprintf(text,sizeof(text),"BATCH FILE %u %s\r\n",s->batch.ids[i],s->batch.done[i]?"DONE":"PENDING");session_impl_queue_text(s,text);if(s->batch.copied[i]) { snprintf(text,sizeof(text),"BATCH COPY %u DONE\r\n",s->batch.ids[i]);session_impl_queue_text(s,text); } }return 1;
    batch_error:session_impl_queue_text(s,ui_text(&s->ui,MSG_BATCH_ERROR));return 1;
    }
    if(!strcmp(line,"ZMAILRESUME") || !strncmp(line,"ZMAILRESUME ",12) || !strcmp(line,"ZMAILUPLOAD") || !strncmp(line,"ZMAILUPLOAD ",12) || !strncmp(line,"ZMAILDOWNLOAD ",14)) {
        if(!s->user) { session_impl_emit(s,"ERR login required\r\n");return 1; }
        if(!s->zmodem_available || !s->binary_allowed) { session_impl_queue_text(s,ui_text(&s->ui,MSG_ZMODEM_UNAVAILABLE));return 1; }
        const char *arg="",*tail;uint32_t id=0;int resume=!strncmp(line,"ZMAILRESUME",11);int receive=resume || !strncmp(line,"ZMAILUPLOAD",11);
        if(receive)arg=line[11]?line+12:"";
        else { if(!session_impl_number_arg(line+14,&id,&tail) || id>VAULT_ID_MAX || (*tail && *tail!=' ')) { session_impl_emit(s,"ERR vault syntax\r\n");return 1; }arg=*tail?tail+1:""; }
        if(strlen(arg)>512 || (*arg && !session_impl_safe_text(arg))) { session_impl_emit(s,"ERR vault syntax\r\n");return 1; }
        s->zmodem_protocol=0;s->zmodem_receive=resume?8:receive?4:5;s->zmodem_id=id;strcpy(s->zmodem_text,arg);s->zmodem_start=1;
        BoardRequest q={0};q.op='@';q.action=1;session_impl_submit(s,&q);if(!s->pending)s->zmodem_start=0;return 1;
    }
    if(!strncmp(line,"ZUPLOADBATCHRESUME ",19) || !strcmp(line,"ZUPLOADBATCHRESUME") || !strncmp(line,"ZUPLOADBATCH ",13) || !strcmp(line,"ZUPLOADBATCH") || !strncmp(line,"ZRESUME ",8) || !strcmp(line,"ZRESUME")) {
        if(!s->user) { session_impl_emit(s,"ERR login required\r\n");return 1; }
        if(!s->zmodem_available || !s->binary_allowed) { session_impl_queue_text(s,ui_text(&s->ui,MSG_ZMODEM_UNAVAILABLE));return 1; }
        int batch_resume=!strncmp(line,"ZUPLOADBATCHRESUME",18);int resume=!strncmp(line,"ZRESUME",7);size_t prefix=batch_resume?18:resume?7:12;const char *arg=line[prefix]?line+prefix+1:((resume || batch_resume)?vault_navigation_path(&s->vault):"-");
        size_t n=strcspn(arg,"|");if(!n || n>48 || strlen(arg)>289 || !session_impl_safe_text(arg)) { session_impl_emit(s,"ERR vault syntax\r\n");return 1; }
        s->zmodem_protocol=0;s->zmodem_receive=batch_resume?9:resume?7:6;s->zmodem_id=0;strcpy(s->zmodem_text,arg);s->zmodem_start=1;
        BoardRequest q={0};q.op='@';session_impl_submit(s,&q);if(!s->pending)s->zmodem_start=0;return 1;
    }
    if(!strncmp(line,"ZUPLOADIN ",10) || !strcmp(line,"ZUPLOAD") || !strncmp(line,"ZUPLOAD ",8) || !strncmp(line,"ZDOWNLOAD ",10)) {
        if(!s->user) { session_impl_emit(s,"ERR login required\r\n");return 1; }
        if(!s->zmodem_available || !s->binary_allowed) { session_impl_queue_text(s,ui_text(&s->ui,MSG_ZMODEM_UNAVAILABLE));return 1; }
        const char *arg="";uint32_t id=0;int receive=!strncmp(line,"ZUPLOAD",7);
        if(!strncmp(line,"ZUPLOADIN ",10)) { receive=3;arg=line+10;size_t n=strcspn(arg,"|");if(!n || n>48 || (arg[n] && strlen(arg+n+1)>240)) { session_impl_emit(s,"ERR vault syntax\r\n");return 1; } }
        else if(receive)arg=line[7]?line+8:"";
        else { const char *tail;if(!session_impl_number_arg(line+10,&id,&tail) || id>VAULT_ID_MAX || (*tail && *tail!=' ')) { session_impl_emit(s,"ERR vault syntax\r\n");return 1; }arg=*tail?tail+1:""; }
        if(strlen(arg)>(receive==3?289u:80u) || (*arg && !session_impl_safe_text(arg))) { session_impl_emit(s,"ERR vault syntax\r\n");return 1; }
        s->zmodem_protocol=0;s->zmodem_receive=receive;s->zmodem_id=id;strcpy(s->zmodem_text,arg);s->zmodem_start=1;
        BoardRequest q={0};q.op='@';session_impl_submit(s,&q);if(!s->pending)s->zmodem_start=0;return 1;
    }
    if(!strcmp(line,"TRANSFER BINARY")) {
        if(!s->binary_allowed) { session_impl_emit(s,"ERR binary negotiation\r\n");return 1; }
        if(!s->board_enabled || !s->user) { session_impl_emit(s,"ERR login required\r\n");return 1; }
        BoardRequest q={0};q.op='@';session_impl_submit(s,&q);return 1;
    }
    if(!strncmp(line,"MFILE",5)) {
        if(s->ui.enabled) { session_impl_emit(s,"ERR vault client\r\n");return 1; }
        BoardRequest q={0};q.op=BOARD_MAIL_TRANSFER;snprintf(q.body,sizeof(q.body),"%s",line+1);session_impl_submit(s,&q);return 1;
    }
    if(transfer_line_command(line)) {
        if(!s->board_enabled) { session_impl_emit(s,"ERR no board\r\n");return 1; }
        if(s->ui.enabled) { session_impl_emit(s,"ERR vault client\r\n");return 1; }
        BoardRequest q={0};q.op='$';snprintf(q.body,sizeof(q.body),"%s",line);session_impl_submit(s,&q);return 1;
    }

    return 0;
}

int session_impl_mail_batch_command(Session *s,char *line) {
    if(!strcmp(line,"ZMAILUPLOADBATCH") || !strncmp(line,"ZMAILUPLOADBATCH ",17) || !strcmp(line,"ZMAILRESUMEBATCH") || !strncmp(line,"ZMAILRESUMEBATCH ",17)) {
        if(!s->user) { session_impl_emit(s,"ERR login required\r\n");return 1; }
        if(!s->zmodem_available || !s->binary_allowed) { session_impl_queue_text(s,ui_text(&s->ui,MSG_ZMODEM_UNAVAILABLE));return 1; }
        const char *description=line[16]?line+17:"";
        if(strlen(description)>80 || (*description && !session_impl_safe_text(description))) { session_impl_emit(s,"ERR command\r\n");return 1; }
        s->zmodem_protocol=0;s->zmodem_receive=line[5]=='R'?12:11;s->zmodem_id=0;snprintf(s->zmodem_text,sizeof(s->zmodem_text),"%s",description);s->zmodem_start=1;
        BoardRequest q={0};q.op='@';q.action=1;session_impl_submit(s,&q);if(!s->pending)s->zmodem_start=0;return 1;
    }

    return 0;
}

int session_impl_mail_batch_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(!strncmp(completion->text,"MAILBATCH",9)) {
        BatchIds ids=batch_response_ids(completion->text+9);
        if(ids.count && batch_format_ids(&ids,s->zmodem_text,sizeof(s->zmodem_text))) { s->zmodem_receive=10;s->zmodem_protocol=s->mail_batch_protocol;s->zmodem_id=0;s->zmodem_start=1;BoardRequest q={0};q.op='@';q.action=1;session_impl_submit(s,&q); }return 1;
    }

    return 0;
}

int session_impl_transfer_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(s->zmodem_start) {
        s->zmodem_start=0;
        if(completion->binary_enter) { s->zmodem=1;session_impl_page_clear(s);s->reader.active=0;if(s->zmodem_protocol)session_impl_queue_text(s,s->ui.language==LANGUAGE_JA?"選択した方式で端末の転送を開始してください。\r\n":"Start terminal transfer using the selected protocol.\r\n");else session_impl_queue_text(s,ui_text(&s->ui,(s->zmodem_receive==6 || s->zmodem_receive==9 || s->zmodem_receive==11 || s->zmodem_receive==12)?MSG_ZMODEM_BATCH_UPLOAD_READY:(s->zmodem_receive==2 || s->zmodem_receive==10)?MSG_ZMODEM_BATCH_READY:MSG_ZMODEM_READY)); }
        else { session_impl_emit(s,completion->text);session_impl_prompt(s); }
        return 1;
    }
    return 0;
}

void session_transfer_receipt(Session *s,const char *text) {
    uint32_t id;
    if(s->zmodem_receive==2 && batch_download_receipt(text,&id))batch_downloaded(&s->batch,id);
}

void session_zmodem_finish(Session *s,int success,unsigned id,uint64_t size,int recovery) {
    s->zmodem=0;s->zmodem_start=0;
    char text[256];
    TransferNotice notice=transfer_notice(success,s->zmodem_receive,s->zmodem_protocol,recovery);
    if(notice==TRANSFER_DETAIL) {
        static const char *ja[]={"再開できる中断データがありません。登録先ボードを確認してください。", "再開時の説明文または単一／一括の指定が一致しません。説明文を省略すると保存済みの説明を使います。", "受信プログラムの再開検証記録がないか一致しません。管理者は liner-notes-check-resume を実行してください。", "転送がタイムアウトしました。受信中はデータの増加がない場合に停止します。"};
        static const char *en[]={"No interrupted transfer in this board. Check the original destination.", "Resume description or single/batch mode differs. Omit the description to reuse the saved value.", "Resume receiver verification is missing or stale. Ask the administrator to run liner-notes-check-resume.", "Transfer timed out. Reception stops when file data makes no progress."};
        snprintf(text,sizeof(text),"%s\r\n",(s->ui.language==LANGUAGE_JA?ja:en)[recovery-2]);
    }
    else if(notice==TRANSFER_PROTOCOL_OK || notice==TRANSFER_PROTOCOL_FAILED) {
        const char *names[]={"ZMODEM","XMODEM","YMODEM","YMODEM-G","M-LINK","TEXT","XMODEM-SUM","XMODEM-1K"};
        if(success)snprintf(text,sizeof(text),s->ui.language==LANGUAGE_JA?"%s 転送完了: %u (%llu バイト)\r\n":"%s transfer completed: %u (%llu bytes)\r\n",names[s->zmodem_protocol],id,(unsigned long long)size);
        else snprintf(text,sizeof(text),s->ui.language==LANGUAGE_JA?"%s 転送は失敗または中断しました。\r\n":"%s transfer failed or cancelled.\r\n",names[s->zmodem_protocol]);
    }
    else if(notice==TRANSFER_MAIL_BATCH)snprintf(text,sizeof(text),s->ui.language==LANGUAGE_JA?"メール添付用に %u ファイル（%llu バイト）を受信しました。MAILFILES で番号を確認できます。\r\n":"Private mail batch ready: %u files (%llu bytes). Use MAILFILES for IDs.\r\n",id,(unsigned long long)size);
    else if(success)snprintf(text,sizeof(text),ui_text(&s->ui,notice==TRANSFER_VAULT_BATCH?MSG_ZMODEM_BATCH_UPLOADED:notice==TRANSFER_MAIL_UPLOAD?MSG_MAIL_UPLOAD_DONE:notice==TRANSFER_BATCH_SENT?MSG_ZMODEM_BATCH_SENT:notice==TRANSFER_RECEIVED?MSG_ZMODEM_RECEIVED:MSG_ZMODEM_SENT),id,(unsigned long long)size);
    else snprintf(text,sizeof(text),"%s",ui_text(&s->ui,notice==TRANSFER_RECOVERY?MSG_ZMODEM_RECOVERY:MSG_ZMODEM_FAILED));
    session_impl_queue_text(s,text);session_impl_prompt(s);
}
