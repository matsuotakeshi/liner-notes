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

int session_impl_preference_command(Session *s,char *line) {
    if(!strcmp(line,"PREFS") || !strcmp(line,"PREFS SAVE") || !strcmp(line,"PREFS LOAD")) {
        if(!s->user) { session_impl_emit(s,"ERR login required\r\n"); return 1; }
        BoardRequest q={0}; q.op='V'; q.number=!strcmp(line,"PREFS SAVE")?1:!strcmp(line,"PREFS LOAD")?2:0;
        session_impl_submit(s,&q); return 1;
    }
    if(!strcmp(line,"PAGER") || !strcmp(line,"PAGER ON") || !strcmp(line,"PAGER OFF")) {
        if(!strcmp(line,"PAGER ON")) s->pager_enabled=1;
        if(!strcmp(line,"PAGER OFF")) s->pager_enabled=0;
        session_impl_emit(s,s->pager_enabled?"PAGER ON\r\n":"PAGER OFF\r\n"); return 1;
    }
    if(!strncmp(line,"PAGER SIZE ",11)) {
        uint32_t cols,rows; const char *tail;
        if(!session_impl_number_arg(line+11,&cols,&tail) || *tail!=' ' || !session_impl_number_arg(tail+1,&rows,&tail) || *tail || cols<20 || cols>240 || rows<5 || rows>100) {
            session_impl_emit(s,"ERR pager size\r\n"); return 1;
        }
        session_dimensions(s,cols,rows); session_impl_emit(s,"PAGER SIZE SET\r\n"); return 1;
    }

    return 0;
}

int session_impl_menu_settings_command(Session *s,char *line) {
    if(!strncmp(line,"MENUMODE ",9)) {
        const char *mode=line+9;int n=!strcmp(mode,"BEGINNER")?0:!strcmp(mode,"STANDARD")?1:!strcmp(mode,"EXPERT")?2:-1;
        if(n<0) { session_impl_emit(s,"ERR command\r\n");return 1; }s->menu_mode=n;s->menu_shown=0;
        if(s->user) { BoardRequest q={0};q.op='}';q.action='v';q.number=(uint32_t)n;session_impl_submit(s,&q); }else session_impl_emit(s,"MENU MODE CHANGED\r\n");return 1;
    }
    if(!s->user && (!strcmp(line,"HANDLE") || !strncmp(line,"HANDLE ",7))) {
        if(line[6]) { if(!session_impl_safe_text(line+7) || !* (line+7) || strlen(line+7)>25) { session_impl_emit(s,"ERR profile text\r\n");return 1; }snprintf(s->guest_handle,sizeof(s->guest_handle),"guest:%s",line+7); }
        snprintf(out,sizeof(out),"GUEST HANDLE %s\r\n",*s->guest_handle?s->guest_handle:"guest");session_impl_emit(s,out);return 1;
    }

    return 0;
}

PreferenceValues session_impl_session_preferences(const Session *s) {
    return (PreferenceValues){s->ui.language,s->pager_enabled,s->editor_line,s->columns,s->rows,
                              (unsigned)s->editor_numbers,s->transfer_protocol};
}

void session_impl_preferences_request(const Session *s,BoardRequest *q) {
    PreferenceValues values=session_impl_session_preferences(s);PreferenceRecord record=preference_pack(&values);
    q->ui_aux=record.aux;q->scope=record.editor;q->audit_article=record.columns;q->audit_actor=record.rows;
    q->cursor=(uint32_t)record.language;q->title[0]=record.pager?'1':'0';
}

int session_disconnect_preferences(const Session *s,BoardRequest *out) {
    PreferenceValues values=session_impl_session_preferences(s);
    if(out)memset(out,0,sizeof(*out));
    if(!preference_disconnect_needed(&s->preferences,s->user,&values))return 0;
    if(out) { out->op='V';out->number=1;out->user=s->user;out->user_epoch=s->user_epoch;session_impl_preferences_request(s,out); }
    return 1;
}

uint64_t session_ui_signature(const Session *s) { PreferenceValues values=session_impl_session_preferences(s);return preference_signature(&values); }
