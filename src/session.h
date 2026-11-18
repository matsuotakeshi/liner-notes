#ifndef SESSION_H
#define SESSION_H
#include "notification_core.h"
#include "connector_core.h"
#include "batch_core.h"
#include "traversal_core.h"
#include "preference_core.h"
#include "mail_navigation_core.h"
#include "list_navigation_core.h"
#include <stddef.h>
#include <stdint.h>
#include "board.h"
#include "ui.h"
/* The adapter owns queuing/backpressure. write copies bytes synchronously and
 * returns 0 on failure. No OS handles, Telnet commands or HTTP frames here. */
typedef struct {
    void *context;
    int (*write)(void *context,const char *text,size_t size);
    unsigned (*online)(void *context);
    /* Optional for buffered transports; NULL means synchronous delivery. */
    int (*drained)(void *context);
} SessionIO;
typedef struct {
    SessionIO io;
    unsigned long id;
    int slot,active,board_enabled,member_mode;
    Ui ui;
    uint64_t connected_at;int talk_once,talk_return_editor,status_pending;uint32_t file_selected,file_selecting;unsigned profile_edit_target;ProfileWalk profile_walk;int batch_screen,batch_copying;BatchSelection batch;int profile_edit_field;int classic_area,classic_input,classic_direction;uint32_t classic_id;
    int door,game_menu;int pager_enabled, help_page,menu_mode,menu_shown,editor_upload_question;
    char guest_handle[33];
    unsigned columns,rows;
    size_t page_offset;
    char page[CONTENT_DISPLAY];
    uint64_t now,wake,auth_after;
    int closing,editing,pending,password_mode;uint32_t reset_target,boardpass_target,board_enter,board_lock;uint32_t board_grants[1025],vault_grants[1025];char filepass_path[49];int filepass_setting;
    ReportNavigation report;
    int gather_mode,gather_pending,service_pending;uint32_t gather_cursor;unsigned gather_lines;uint64_t sysop_mail_after;NotificationState notifications;
    int editor_line,editor_input,editor_numbers,editor_upload,editor_confirm;uint64_t editor_deadline;uint32_t editor_at;
    char upload_description[241],upload_filename[101];int upload_batch;
    VaultNavigation vault;
    unsigned mail_batch_protocol;
    MailNavigation mail;
    int outside_talk,mail_mode,mail_pending,mail_input; uint32_t mail_revision;
    unsigned search_first,search_last;int memo_input;int chatting,chat_notice,chat_pending,chat_prompting,talk_select,talk_lost;
    unsigned long talk_target,reply_target;
    PreferenceLifecycle preferences;unsigned transfer_protocol,zmodem_protocol;int zmodem_available,zmodem,zmodem_start,zmodem_receive,vault_mode;uint32_t zmodem_id;char zmodem_text[513];
    int binary,binary_allowed,binary_menu;
    ConnectorReader binary_reader;
    uint32_t current,user,board,user_epoch;unsigned member_level;
    char username[33];
    /* Opt-in transport synchronization; never an authentication credential. */
    char connector_token[65];
    int connector_literal;
    ConnectorBlock connector_block;
    char old_password[129], new_password[129];
    BoardRequest draft, scan;
    CheckTraversal check;
    ReaderState reader;
    int application_step;char application_body[8193];int profile_filter;unsigned profile_group;
    uint32_t draft_revision;
    int undo_available;
    char undo_title[BOARD_TITLE+1], undo_body[BOARD_BODY+1];
} Session;
void session_init(Session *,SessionIO,unsigned long id,int slot,int boards,int members,int menu,Language);
void session_dimensions(Session *,unsigned columns,unsigned rows);
void session_abort(Session *);
void session_door_finish(Session *);
void session_scan_stop(Session *);
void session_scan_pause(Session *);
/* Invalidate board listings without changing pending/automatic reader state. */
void session_board_refresh(Session *);
/* Invalidate only access-log paging, keeping outstanding worker state. */
void session_access_refresh(Session *);
int session_auto_read(const Session *);
void session_chat_kick(Session *);
void session_revoke(Session *);
uint64_t session_ui_signature(const Session *);
/* NULL queries eligibility only. Otherwise zero/fill a caller-owned request.
 * Runtime allocates/enqueues it; Member Core still authorizes the saved epoch. */
int session_disconnect_preferences(const Session *,BoardRequest *);
void session_dispose(Session *);
int session_ready(const Session *);
int session_secret(const Session *);
int session_uppercase(const Session *,const char *,size_t);
/* Caller retains a line while not ready; one accepted line per scheduler turn.
 * Secret input must not be echoed by adapters. Lines exclude CR/LF. */
int session_line(Session *,const char *text,size_t size,uint64_t now);
void session_announce(Session *,const char *);
void session_tick(Session *,uint64_t now);
/* Runtime validates session/slot/transfer generation before delivering receipts. */
void session_transfer_receipt(Session *,const char *);
void session_zmodem_finish(Session *,int success,unsigned id,uint64_t size,int recovery);
void session_mail_arrived(Session *,uint32_t recipient);
void session_mail_notify(Session *,uint32_t user,int deny,uint32_t revision);
int session_binary_byte(Session *,unsigned char,uint64_t now);
void session_result(Session *,const BoardResult *,uint64_t now);
#endif
