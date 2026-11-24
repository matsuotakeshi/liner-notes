#ifndef SESSION_INTERNAL_H
#define SESSION_INTERNAL_H
/* Private adapter collaboration only. Runtime/worker/transport use session.h.
 * All handlers run synchronously on one Session event loop. A handled result
 * stops routing; it is NOT an authorization decision. Worker ACLs remain final.
 * Completion handlers must only be called after session_result validates identity.
 * Borrowed input/result pointers may not be retained. submit copies requests.
 */
#include "session.h"
#include "session_io.h"
#include "session_state.h"
/* Only the selected RTBBS adapter (and focused white-box tests) uses this cast. */
static inline SessionRtbbsState *rtbbs(Session *s) { return (SessionRtbbsState *)s->ui_binding.state; }
static inline const SessionRtbbsState *rtbbs_const(const Session *s) { return (const SessionRtbbsState *)s->ui_binding.state; }
#include "editor_core.h"
#include "chat.h"

/* RTBBS implementation callbacks; shared entry points dispatch through session_ui. */
void session_rtbbs_result(Session *,const OperationResult *,SessionResultView,uint64_t,int);
void session_rtbbs_tick(Session *,uint64_t);
void session_rtbbs_emit(Session *,const char *);
void session_rtbbs_prompt(Session *);
void session_rtbbs_menu(Session *,int);
void session_rtbbs_help(Session *,const char *);
int session_rtbbs_uppercase(const Session *,const char *,size_t);
void session_rtbbs_door_finish(Session *);
void session_rtbbs_chat_kick(Session *s);
int session_rtbbs_binary_byte(Session *s,unsigned char byte,uint64_t now);
void session_rtbbs_scan_pause(Session *s);
void session_rtbbs_scan_stop(Session *s);
void session_rtbbs_board_refresh(Session *s);
void session_rtbbs_access_refresh(Session *s);
void session_rtbbs_announce(Session *s,const char *text);
void session_rtbbs_mail_arrived(Session *s,uint32_t recipient);
void session_rtbbs_mail_notify(Session *s,uint32_t user,int deny,uint32_t revision);
int session_rtbbs_disconnect_preferences(const Session *s,BoardRequest *out);
uint64_t session_rtbbs_ui_signature(const Session *s);
void session_rtbbs_transfer_receipt(Session *s,const char *text);
void session_rtbbs_zmodem_finish(Session *s,int success,unsigned id,uint64_t size,int recovery);
void session_rtbbs_cleanup(Session *,int,int);
void session_rtbbs_prepare_request(Session *,OperationRequest *);

enum { CI_NONE, CI_DATE, CI_AUTHOR, CI_HANDLE, CI_PROFILE, CI_PROFILE_SEARCH,
       CI_MEMO, CI_WITHDRAW, CI_HEADER, CI_TITLE, CI_OWNER, CI_EDITOR, CI_MENUMODE, CI_ROWS,
       CI_FILEINFO, CI_DOWNLOAD, CI_UPLOAD, CI_EXIT, CI_PROTOCOL, CI_FILENAME, CI_FILESIZE, CI_BATCH, CI_BATCH_ADD, CI_BATCH_REMOVE, CI_BATCH_MATCH,CI_PROF_FIELD,CI_PROF_VALUE,CI_SEARCH_FIELD,CI_SEARCH_VALUE,CI_BATCH_CLEAR,CI_SEND_PROTOCOL,CI_FILE_ACTION,CI_FILE_DESC,CI_FILE_DELETE,CI_PROF_TARGET,CI_PROF_NEXT,CI_BATCH_COPY };
enum { CM_MAIN, CM_BOARD, CM_SETTINGS, CM_PROFILE, CM_ADMIN };

int session_impl_application_input(Session *s,char *line);
int session_impl_password_input(Session *s,char *line);
int session_impl_password_reset_command(Session *s,char *line);
int session_impl_password_change_command(Session *s,char *line);
int session_impl_account_command(Session *s,char *line);
int session_impl_authentication_result(Session *s,const BoardResult *completion,uint64_t now);
int session_impl_password_prompt_result(Session *s,const BoardResult *completion,uint64_t now);
int session_impl_account_flags_result(Session *s,const BoardResult *completion,uint64_t now);
int session_impl_editor_preinput(Session *s,char **input,char *scratch);
int session_impl_editor_input(Session *s,char *line);
int session_impl_editor_mode_command(Session *s,char *line);
int session_impl_draft_command(Session *s,char *line);
int session_impl_buffer_result(Session *s,const BoardResult *completion,uint64_t now);
int session_impl_board_editor_result(Session *s,const BoardResult *completion,uint64_t now);
void session_impl_undo_clear(Session *s);
EditorOutcome session_impl_edit_apply(Session *s,EditorOperation op,uint32_t first,uint32_t last,const char *text);
unsigned session_impl_editor_count(const Session *s);
int session_impl_board_options(Session *s,char *line);
int session_impl_board_structure_command(Session *s,char *line);
int session_impl_board_unlock_result(Session *s,const BoardResult *completion,uint64_t now);
int session_impl_conversation_command(Session *s,char *line);
int session_impl_status_result(Session *s,const BoardResult *completion,uint64_t now);
int session_impl_chat_result(Session *s,const BoardResult *completion,uint64_t now);
int session_impl_chat_event(void *context,const ChatEvent *e);
int session_impl_talk_number(const char *text,unsigned long *id);
void session_impl_session_status(Session *s);
void session_impl_chat_input(Session *s,char *line);
int session_impl_member_rights_command(Session *s,char *line);
int session_impl_member_directory_command(Session *s,char *line);
int session_impl_preference_command(Session *s,char *line);
int session_impl_menu_settings_command(Session *s,char *line);
PreferenceValues session_impl_session_preferences(const Session *s);
void session_impl_preferences_request(const Session *s,BoardRequest *q);
int session_impl_search_command(Session *s,char *line);
int session_impl_audit_request(Session *s,char *line);
int session_impl_article_command(Session *s,char *line);
int session_impl_traversal_result(Session *s,const BoardResult *completion,uint64_t now);
int session_impl_profile_walk_result(Session *s,const BoardResult *completion,uint64_t now);
int session_impl_report_result(Session *s,const BoardResult *completion,uint64_t now);
int session_impl_article_result(Session *s,const BoardResult *completion,uint64_t now);
void session_impl_check_page(Session *s);
void session_impl_profile_continue(Session *s);
int session_impl_transfer_command(Session *s,char *line);
int session_impl_mail_batch_command(Session *s,char *line);
int session_impl_mail_batch_result(Session *s,const BoardResult *completion,uint64_t now);
int session_impl_transfer_result(Session *s,const BoardResult *completion,uint64_t now);
int session_impl_vault_command(Session *s,char *line);
int session_impl_vault_children_result(Session *s,const BoardResult *completion,uint64_t now);
int session_impl_batch_copy_result(Session *s,const BoardResult *completion,uint64_t now);
int session_impl_vault_edit_result(Session *s,const BoardResult *completion,uint64_t now);
int session_impl_door_command(Session *s,char *line);
void session_impl_command(Session *s,char *line);
void session_rtbbs_command(Session *s,char *line);
int session_impl_message_result(Session *s,const BoardResult *completion,uint64_t now);
int session_impl_service_command(Session *s,char *line);
int session_impl_conference_command(Session *,char *);
int session_impl_conference_result(Session *,const OperationResult *);
void session_impl_gather_input(Session *s,const char *line);
int session_impl_mail_result(Session *s,const BoardResult *completion,uint64_t now);
MailSessionEvent session_impl_mail_event(int action);
void session_impl_mail_submit(Session *s,BoardRequest *q);
int session_impl_mail_command(Session *s,char *line);
void session_impl_web_view(Session *,const char *);
int session_impl_web_idle(Session *,int editing,int chatting);
void session_impl_web_update(Session *,char *);
int session_impl_web_update_result(Session *,const OperationResult *,SessionResultView);
const char *session_impl_chat_delivery(Session *,const OperationResult *,uint64_t,MessageId *);
int session_impl_web_view_result(Session *,const OperationResult *,SessionResultView);
int session_impl_binary_result(Session *s,const BoardResult *completion,uint64_t now);
void session_impl_normalize_command(Session *s,char *line,size_t size);
int session_impl_connector_line(Session *s,char *line,size_t size);
void session_impl_binary_frame(Session *s,unsigned char kind,const void *data,size_t size);
void session_impl_queue_text(Session *s,const char *text);
void session_impl_page_clear(Session *s);
void session_impl_page_next(Session *s);
void session_impl_emit(Session *s,const char *text);
void session_impl_show_menu(Session *s,int explicit_request);
void session_impl_prompt(Session *s);
void session_impl_show_help(Session *s,const char *topic);
int session_impl_number_arg(const char *p,uint32_t *number,const char **rest);
int session_impl_audit_number(const char *p,uint32_t *out);
int session_impl_audit_arguments(const char *args,BoardRequest *q);
void session_impl_audit_command(const Ui *ui,uint32_t cursor,char *out,size_t size);
int session_impl_safe_text(const char *p);
int session_impl_find_arguments(const char *arg,BoardRequest *q);
int session_impl_extended_command(Session *s,char *line);
int session_impl_classic_input(Session *s,const char *line);
int session_impl_classic_commands(Session *s,const char *line);
void session_impl_selection_reset(Session *s);
void session_impl_submit(Session *s,BoardRequest *q);
#endif
