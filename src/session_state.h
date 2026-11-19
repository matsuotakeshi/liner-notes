#ifndef SESSION_STATE_H
#define SESSION_STATE_H
#include "notification_core.h"
#include "connector_core.h"
#include "batch_core.h"
#include "traversal_core.h"
#include "preference_core.h"
#include "mail_navigation_core.h"
#include "list_navigation_core.h"
#include "message_flow_core.h"
#include "form_flow_core.h"
#include <stddef.h>
#include <stdint.h>
#include "board.h"
#include "ui.h"
typedef struct {
    int password_mode;
    char login[33]; /* unverified name belongs to the authentication dialogue */
    uint32_t reset_target;
    uint32_t boardpass_target;
    uint32_t board_enter;
    uint32_t board_lock;
    char filepass_path[49];
    int filepass_setting;
    char old_password[129];
    char new_password[129];
    ApplicationForm application;
} SessionAuth;
typedef struct {
    Ui ui;
    int classic_area;
    int classic_input;
    int classic_direction;
    uint32_t classic_id;
    int game_menu;
    int pager_enabled;
    int help_page;
    int menu_mode;
    int menu_shown;
    size_t page_offset;
    char page[CONTENT_DISPLAY];
    PreferenceLifecycle preferences;
} SessionPresentation;
typedef struct {
    int editor_upload_question;
    int editing;
    int editor_line;
    int editor_input;
    int editor_numbers;
    int editor_upload;
    int editor_confirm;
    uint64_t editor_deadline;
    uint32_t editor_at;
    BoardRequest draft;
    uint32_t draft_revision;
    int undo_available;
    char undo_title[BOARD_TITLE+1];
    char undo_body[BOARD_BODY+1];
} SessionEditor;
typedef struct {
    ProfileForm profile_form;
    ProfileWalk profile_walk;
    ReportNavigation report;
    unsigned search_first;
    unsigned search_last;
    BoardRequest scan;
    CheckTraversal check;
    ReaderState reader;
    int profile_filter;
    unsigned profile_group;
} SessionReading;
typedef struct {
    uint32_t file_selected;
    uint32_t file_selecting;
    int batch_screen;
    int batch_copying;
    BatchSelection batch;
    VaultNavigation vault;
    int vault_mode;
} SessionFiles;
typedef struct {
    unsigned mail_batch_protocol;
    MailNavigation mail;
    int mail_mode;
    int mail_pending;
    int mail_input;
    uint32_t mail_revision;
} SessionMailState;
typedef struct {
    int talk_once;
    int talk_return_editor;
    int status_pending;
    GatherFlow gather;
    SysopMailFlow sysop_mail;
    NotificationState notifications;
    int outside_talk;
    int memo_input;
    int chatting;
    int chat_notice;
    int chat_pending;
    int chat_prompting;
    int talk_select;
    int talk_lost;
    unsigned long talk_target;
    unsigned long reply_target;
} SessionConversation;
/* Private RTBBS interaction state. Core and transports do not include this header. */
typedef struct {
    int initialized;
    SessionAuth auth;
    SessionPresentation presentation;
    SessionEditor editor;
    SessionReading reading;
    SessionFiles files;
    SessionMailState mail_state;
    SessionConversation conversation;
} SessionRtbbsState;
#endif
