#ifndef BOARD_H
#define BOARD_H
#include <stdint.h>
#include "chat_preferences.h"
#define BOARD_MEMBER_LIFECYCLE '{'
#define BOARD_MAIL_TRANSFER '='
#define BOARD_TITLE 80
#include "content_limits.h"
#include "mail_attachments.h"
#define BOARD_BODY CONTENT_BODY
#define BOARD_REPLY (CONTENT_BODY+8192)
typedef struct {
    unsigned long session,chat_target;
    int slot;
    char op;
    int notes_unread, action, scope, zcontrol,descending;uint64_t since,until;
    uint32_t number;
    uint32_t cursor;
    unsigned ui_aux;uint32_t audit_article,audit_actor,mail_revision,mail_attachment; MailAttachments mail_attachments;
    uint32_t user;
    uint32_t user_epoch;
    uint32_t board_grants[1025],vault_grants[1025];
    uint32_t board;
    char username[33], password[129], new_password[129];
    char title[BOARD_TITLE+1], body[BOARD_BODY+1];
} BoardRequest;
typedef struct { uint64_t since,until;MailAttachments mail_attachments; unsigned long session; int slot; uint32_t selected, board; int auth, editing; uint32_t user_epoch,account_user,account_epoch; unsigned long chat_target; int zcontrol; int service_action; uint32_t service_number,service_next; char service_text[513]; int mail_notify_deny; uint32_t mail_notify_user,mail_notify_revision; int mail_action; uint32_t mail_id,mail_to,mail_parent,mail_next,mail_notice,mail_revision,mail_draft_notice,mail_delivery,mail_attachment; uint32_t account_user2,account_epoch2; uint32_t board_grant,board_revision; int board_refresh,access_refresh,sigop; int chat_admin; int chat_action; uint32_t chat_user,chat_kick_user; unsigned chat_kick_count; ChatPreferences chat_preferences; char chat_name[81],chat_login[33]; int birthday;int revoked, password_changed, preferences, language, pager; int menu_mode;int all_accounts_changed;int member_level;unsigned ui_aux;int editor_preference;unsigned columns,rows;int direct, sequence, deferred, binary_enter; unsigned binary_size; uint32_t sequence_next; int draft_loaded; uint32_t saved_draft_notice, unread_articles_notice, unread_boards_notice; uint32_t draft_revision,draft_parent; char draft_title[81],draft_body[CONTENT_BODY+1]; char text[BOARD_REPLY]; } BoardResult;
int board_start(const char *path,const char *members,const char *catalog);
int board_start_config(const char *path,const char *members,const char *catalog,const char *admins);
int board_save_disconnect(const BoardRequest *);
int board_submit(const BoardRequest *request);
int board_poll(BoardResult *result);
void board_stop(void);
int board_disable_feature(const char *name);
int board_feature_enabled(const char *name);
#endif
