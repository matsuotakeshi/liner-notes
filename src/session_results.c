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

void session_result(Session *s,const BoardResult *completion,uint64_t now) {

    if(!s->active || s->closing || s->id!=completion->session || s->slot!=completion->slot) return;
    s->now=now; s->pending=0;
    if(reader_cancel_completion(&s->reader,completion->sequence)) { session_impl_queue_text(s,ui_text(&s->ui,MSG_SCAN_STOP));session_impl_prompt(s);return; }
    if(s->preferences.exit_action) {
        PreferenceExit action=preference_exit_complete(&s->preferences,!strncmp(completion->text,"PREFS SAVED",11));
        if(action!=PREF_EXIT_NONE) { session_impl_command_core(s,action==PREF_EXIT_LOGOUT?"LOGOUT":"QUIT");session_impl_prompt(s);return; }
        session_impl_emit(s,completion->text);session_impl_prompt(s);return;
    }
    if(completion->sigop>=0)s->ui.sigop=completion->sigop;
    if(completion->service_action=='E') { s->ui.input=completion->service_number==2?UI_REPLY:UI_POST;session_impl_prompt(s);return; }
    if(completion->mail_notify_user)session_mail_notify(s,completion->mail_notify_user,completion->mail_notify_deny,completion->mail_notify_revision);
    if(completion->chat_user)chat_apply_preferences(completion->chat_user,&completion->chat_preferences);
    if(completion->revoked) { session_revoke(s); return; }
    if(vault_navigation_complete(&s->vault,!strncmp(completion->text,"FILEBOARDSETTING ",17))) {
        s->file_selected=0;s->vault_mode=1;s->mail_mode=0;
    }
    mail_navigation_observe(&s->mail,session_impl_mail_event(completion->mail_action),completion->mail_id);
    if(session_impl_traversal_result(s,completion,now))return;
    if(session_impl_mail_batch_result(s,completion,now))return;
    if(session_impl_vault_children_result(s,completion,now))return;
    if(session_impl_status_result(s,completion,now))return;
    if(session_impl_profile_walk_result(s,completion,now))return;
    if(session_impl_batch_copy_result(s,completion,now))return;
    if(session_impl_report_result(s,completion,now))return;
    if(!strncmp(completion->text,"FILEBOARD UNLOCKED ",19)) { unsigned id,revision;if(sscanf(completion->text,"FILEBOARD UNLOCKED %u %u",&id,&revision)==2 && id<=1024)s->vault_grants[id]=revision; }
    if(completion->auth) { s->session_impl_classic_input=0;s->classic_id=0;s->classic_area=CM_MAIN;check_traversal_reset(&s->check);memset(s->vault_grants,0,sizeof(s->vault_grants)); }
    if(session_impl_vault_edit_result(s,completion,now))return;
    if(session_impl_message_result(s,completion,now))return;
    if(session_impl_mail_result(s,completion,now))return;
    if(session_impl_chat_result(s,completion,now))return;
    if(session_impl_transfer_result(s,completion,now))return;
    if(session_impl_binary_result(s,completion,now))return;
    if(session_impl_authentication_result(s,completion,now))return;
    if(session_impl_board_unlock_result(s,completion,now))return;
    if(session_impl_buffer_result(s,completion,now))return;
    if(session_impl_password_prompt_result(s,completion,now))return;
    if(session_impl_board_editor_result(s,completion,now))return;
    if(session_impl_account_flags_result(s,completion,now))return;
    if(session_impl_article_result(s,completion,now))return;
}
