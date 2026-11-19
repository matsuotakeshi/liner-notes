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

void session_impl_binary_frame(Session *s,unsigned char kind,const void *data,size_t size) {
    unsigned char frame[BOARD_REPLY+6];size_t written;
    if(!connector_frame_encode(kind,data,size,BOARD_REPLY,frame,sizeof(frame),&written)) { session_abort(s);return; }
    if(!s->connection.io.write(s->connection.io.context,(char *)frame,written))session_abort(s);
}

void session_impl_queue_text(Session *s,const char *text) {
    if(s->core.active && !s->connection.io.write(s->connection.io.context,text,strlen(text))) session_abort(s);
}

void session_impl_page_clear(Session *s) { OPENSSL_cleanse(s->presentation.page,sizeof(s->presentation.page)); s->presentation.page_offset=0; s->presentation.help_page=0; }

void session_impl_page_next(Session *s) {
    char out[8192];int more=terminal_pager_render(s->presentation.page,&s->presentation.page_offset,s->presentation.columns,s->presentation.rows,out,sizeof(out));
    if(more<0) { session_abort(s);return; }
    session_impl_queue_text(s,out);
    if(more)session_impl_queue_text(s,ui_text(&s->presentation.ui,MSG_PAGER_MORE));
    else session_impl_page_clear(s);
}

void session_rtbbs_emit(Session *s,const char *text) {
    char formatted[CONTENT_DISPLAY];
    if(s->presentation.ui.enabled) {
        if(!ui_format(&s->presentation.ui,text,formatted,sizeof(formatted))) { session_abort(s); return; }
        if(s->presentation.pager_enabled && !s->reading.reader.automatic && terminal_pager_response(text)) {
            snprintf(s->presentation.page,sizeof(s->presentation.page),"%s",formatted); s->presentation.page_offset=0; session_impl_page_next(s);
        } else session_impl_queue_text(s,formatted);
    } else session_impl_queue_text(s,text);
}

void session_rtbbs_menu(Session *s,int explicit_request) {
    if(s->presentation.classic_area) {
        unsigned part=s->presentation.classic_area==CM_BOARD?(s->presentation.ui.view==UI_ARTICLE?1:0):s->presentation.classic_area==CM_SETTINGS?2:s->presentation.classic_area==CM_PROFILE?3:s->presentation.ui.admin?4:5;
        static const MessageId fallback[]={MSG_CLASSIC_BOARD_MENU,MSG_CLASSIC_READER_MENU,MSG_CLASSIC_SETTINGS_MENU,MSG_CLASSIC_PROFILE_MENU,MSG_CLASSIC_ADMIN_MENU,MSG_CLASSIC_SIGOP_MENU};
        const char *text=menu_section_text(s->presentation.ui.language,part);
        session_impl_queue_text(s,text?text:ui_text(&s->presentation.ui,fallback[part]));return;
    }
    if(explicit_request || !s->presentation.menu_mode || (s->presentation.menu_mode==1 && !s->presentation.menu_shown))session_impl_queue_text(s,ui_menu(&s->presentation.ui));
    s->presentation.menu_shown=1;
}

void session_rtbbs_prompt(Session *s) {
    if(s->reading.reader.automatic || s->connection.door || s->conversation.memo_input || s->auth.application.step || (s->editor.editing && s->editor.editor_input==4))return;
    if(!s->connection.zmodem && !s->conversation.chatting && !s->connection.binary && s->presentation.ui.enabled && s->core.active && !s->core.pending && !s->core.closing && !s->core.wake && !s->presentation.page[0]) {
        if(s->presentation.game_menu && !s->editor.editing && !s->auth.password_mode) { session_impl_queue_text(s,s->presentation.ui.language==LANGUAGE_JA?"ゲーム [番号:起動 Enter/L:一覧 0:主画面] > ":"Games [number:play Enter/L:list 0:main] > ");return; }
        if(s->conversation.gather.mode) { session_impl_queue_text(s,ui_text(&s->presentation.ui,s->conversation.gather.cursor?MSG_GATHER_MORE:MSG_GATHER_PROMPT));return; }
        if(s->files.vault_mode && !s->editor.editing && !s->auth.password_mode && !s->presentation.ui.input && (!s->presentation.classic_input || s->presentation.classic_input==CI_FILE_ACTION || s->presentation.classic_input==CI_BATCH)) {
            if(s->files.batch_screen) { s->presentation.classic_input=CI_BATCH;session_impl_queue_text(s,ui_text(&s->presentation.ui,MSG_TRANSFER_BATCH));return; }
            if(s->files.file_selected) { s->presentation.classic_input=CI_FILE_ACTION;session_impl_queue_text(s,s->presentation.ui.language==LANGUAGE_JA?"T:表示 S/C/X/Y/G/Z/M:転送 B:選択 E:説明 K:削除 0:戻る > ":"T:view S/C/X/Y/G/Z/M:transfer B:select E:description K:delete 0:back > ");return; }
        }
        if(s->presentation.classic_input || (s->editor.editing && (s->editor.editor_confirm || s->editor.editor_upload_question)))return;
        if(s->editor.editing && s->editor.editor_line && !s->editor.editor_input) { session_impl_queue_text(s,ui_text(&s->presentation.ui,MSG_EDITOR_PROMPT));return; }
        if(s->editor.editing && s->editor.editor_numbers) { char number[40];snprintf(number,sizeof(number),"%u ",s->editor.editor_line?s->editor.editor_at:session_impl_editor_count(s)+1);session_impl_queue_text(s,number); }
        if(s->files.vault_mode && !s->editor.editing && !s->auth.password_mode && !s->presentation.ui.input) { session_impl_queue_text(s,ui_text(&s->presentation.ui,MSG_VAULT_PROMPT));return; }
        if(s->mail_state.mail_input) { session_impl_queue_text(s,ui_text(&s->presentation.ui,s->mail_state.mail_input==1?MSG_MAIL_TO_PROMPT:MSG_MAIL_TITLE_PROMPT));return; }
    if(s->mail_state.mail.delete_id) { session_impl_queue_text(s,ui_text(&s->presentation.ui,MSG_MAIL_CONFIRM));return; }
        if(s->mail_state.mail_mode && !s->editor.editing && !s->auth.password_mode) { session_impl_queue_text(s,ui_text(&s->presentation.ui,MSG_MAIL_PROMPT));return; }
        if(s->reading.reader.active && !s->reading.reader.automatic) session_impl_queue_text(s,ui_text(&s->presentation.ui,MSG_SCAN_HINT));
        if(!s->editor.editing && !s->auth.password_mode && !s->presentation.ui.input && !s->reading.reader.active && s->presentation.ui.view==UI_HOME)session_impl_show_menu(s,0);
        char text[512];size_t prefix=0;
        if(!s->editor.editing && !s->auth.password_mode && !s->presentation.ui.input)
            prefix=(size_t)snprintf(text,sizeof(text),"%s",s->presentation.classic_area==CM_BOARD?(s->presentation.ui.view==UI_ARTICLE?"READ ":"BOARD "):s->presentation.classic_area==CM_SETTINGS?"INSTALL ":s->presentation.classic_area==CM_PROFILE?"PROFILE ":"MAIN ");
        /* One write: asynchronous notices redraw pending terminal input once. */
        ui_prompt(&s->presentation.ui,s->core.board,s->core.current,s->core.user?s->core.username:*s->core.guest_handle?s->core.guest_handle:ui_text(&s->presentation.ui,MSG_GUEST),s->editor.editing,s->auth.password_mode,text+prefix,sizeof(text)-prefix);
        session_impl_queue_text(s,text);
    }
}

void session_rtbbs_help(Session *s,const char *topic) {
    static const char *names[]={"", "READ", "UNREAD", "POST", "DRAFT", "SETTINGS", "ACCOUNT", "ADMIN", "CONTENT", "PROFILE", "FILES", "CHAT", "TALK", "MAIL", "GATHER", "SIGOP", "EDITOR", "ADMINMEMBERS", "ADMINBOARDS", "ADMINFILES", "ADMINCONNECTIONS", "ADMINSTORAGE"};
    static const MessageId ids[]={MSG_HELP_INDEX,MSG_HELP_READ,MSG_HELP_UNREAD,MSG_HELP_POST,
                                  MSG_HELP_DRAFT,MSG_HELP_SETTINGS,MSG_HELP_ACCOUNT,MSG_HELP_ADMIN,MSG_HELP_CONTENT,MSG_HELP_PROFILE,MSG_HELP_FILES,MSG_HELP_CHAT,MSG_HELP_CHAT,MSG_HELP_MAIL,MSG_HELP_GATHER,MSG_HELP_SIGOP,MSG_EDITOR_HELP,MSG_HELP_ADMINMEMBERS,MSG_HELP_ADMINBOARDS,MSG_HELP_ADMINFILES,MSG_HELP_ADMINCONNECTIONS,MSG_HELP_ADMINSTORAGE};
    char key[32]; size_t n=strlen(topic);
    if(n>=sizeof(key)) { session_impl_queue_text(s,ui_text(&s->presentation.ui,MSG_HELP_UNKNOWN)); if(!s->presentation.ui.enabled) session_impl_queue_text(s,"END HELP\r\n"); return; }
    for(size_t i=0;i<=n;i++) key[i]=topic[i]>='a' && topic[i]<='z'?topic[i]-32:topic[i];
    MessageId id=MSG_HELP_UNKNOWN;
    for(size_t i=0;i<sizeof(ids)/sizeof(ids[0]);i++) if(!strcmp(key,names[i])) { id=ids[i]; break; }
    if((!strncmp(key,"ADMIN",5) && !s->presentation.ui.admin) || (id==MSG_HELP_SIGOP && !s->presentation.ui.admin && !s->presentation.ui.sigop)) { session_impl_emit(s,"ERR admin required\r\n");if(!s->presentation.ui.enabled)session_impl_queue_text(s,"END HELP\r\n");return; }
    const char *external=help_text(s->presentation.ui.language,key);const char *text=external?external:ui_text(&s->presentation.ui,id);
    char filtered[CONTENT_DISPLAY];
    if(id==MSG_HELP_INDEX && !s->presentation.ui.admin) {
        snprintf(filtered,sizeof(filtered),"%s",text);char *entry=strstr(filtered,"HELP ADMIN");
        if(entry) { char *end=strstr(entry,"\r\n");if(end)memmove(entry,end+2,strlen(end+2)+1);else *entry=0; }text=filtered;
    }
    if(id==MSG_HELP_INDEX && (s->presentation.ui.admin || s->presentation.ui.sigop)) {
        if(text!=filtered)snprintf(filtered,sizeof(filtered),"%s",text);
        size_t have=strlen(filtered);snprintf(filtered+have,sizeof(filtered)-have,"%s",ui_text(&s->presentation.ui,MSG_HELP_SIGOP_LINK));text=filtered;
    }
    if(s->presentation.ui.enabled && s->presentation.pager_enabled) {
        snprintf(s->presentation.page,sizeof(s->presentation.page),"%s",text); s->presentation.page_offset=0; s->presentation.help_page=1; session_impl_page_next(s);
    } else {
        session_impl_queue_text(s,text);
        if(!s->presentation.ui.enabled) session_impl_queue_text(s,"END HELP\r\n");
    }
}
