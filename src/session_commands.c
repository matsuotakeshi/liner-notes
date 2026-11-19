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

static int utility_command(Session *s,char *line);

static int utility_command(Session *s,char *line) {
    char out[128];
    if(!strcmp(line,"PING")) session_impl_emit(s,"PONG\r\n");
    else if(!strcmp(line,"WHO")) {
        unsigned n=s->io.online?s->io.online(s->io.context):1;
        snprintf(out,sizeof(out),"WHO %u\r\n",n); session_impl_emit(s,out);
    } else if(!strncmp(line,"ECHO ",5)) { session_impl_emit(s,line+5); session_impl_emit(s,"\r\n"); }
    else if(!strncmp(line,"WAIT ",5)) {
        char *end; long n=strtol(line+5,&end,10);
        if(end==line+5 || *end || n<1 || n>5000) session_impl_emit(s,"ERR WAIT 1..5000\r\n");
        else { s->wake=s->now+(uint64_t)n; session_impl_emit(s,"WAITING\r\n"); }
    } else if(!strcmp(line,"QUIT")) {
        if(preference_exit_begin(&s->preferences,s->user,PREF_EXIT_QUIT)) { session_impl_command_core(s,"PREFS SAVE");preference_exit_submitted(&s->preferences,s->pending);return 1; }
        s->preferences.exit_action=PREF_EXIT_NONE;if(s->ui.enabled)session_impl_queue_text(s,site_text(1));session_impl_emit(s,"BYE\r\n");s->closing=1;
    }
    else session_impl_emit(s,"ERR command\r\n");

    return 1;
}

void session_impl_command_core(Session *s,char *line) {
    if(session_impl_application_input(s,line))return;
    if(members_auto_ids() && !strcmp(line,"REGISTER"))line="REGISTER auto";
    char scratch[INPUT_LINE_MAX+2];
    if(session_impl_editor_preinput(s,&line,scratch) || session_impl_password_input(s,line) || session_impl_editor_input(s,line))return;
    static int (*const handlers[])(Session *,char *)={
        session_impl_board_options,
        session_impl_password_reset_command,
        session_impl_editor_mode_command,
        session_impl_service_command,
        session_impl_mail_command,
        session_impl_conversation_command,
        session_impl_extended_command,
        session_impl_draft_command,
        session_impl_member_rights_command,
        session_impl_preference_command,
        session_impl_search_command,
        session_impl_password_change_command,
        session_impl_transfer_command,
        session_impl_vault_command,
        session_impl_audit_request,
        session_impl_member_directory_command,
        session_impl_door_command,
        session_impl_menu_settings_command,
        session_impl_board_structure_command,
        session_impl_mail_batch_command,
        session_impl_account_command,
        session_impl_article_command,
        utility_command,
    };
    for(size_t i=0;i<sizeof(handlers)/sizeof(*handlers);i++)if(handlers[i](s,line))return;
}
