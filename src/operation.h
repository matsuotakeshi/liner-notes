#ifndef OPERATION_H
#define OPERATION_H
#include <stdint.h>
#include <stddef.h>
#include "chat_preferences.h"
#include "operation_pages.h"
#define BOARD_MEMBER_LIFECYCLE '{'
#define BOARD_MAIL_TRANSFER '='
#define BOARD_LINK '\x1e'
#define BOARD_TITLE 80
#include "content_limits.h"
#include "mail_attachments.h"
#define BOARD_BODY CONTENT_BODY
#define BOARD_REPLY (CONTENT_BODY+8192)
typedef struct {
    unsigned long session,chat_target;
    int slot;
    char op;
    int notes_unread, action, scope, zcontrol,descending;int structured;uint64_t since,until;
    uint32_t number;
    uint32_t cursor;
    unsigned ui_aux;uint32_t audit_article,audit_actor,mail_revision,mail_attachment; MailAttachments mail_attachments;
    uint32_t user;
    uint32_t user_epoch;
    uint32_t board_grants[1025],vault_grants[1025];
    uint32_t board;
    char username[33], password[129], new_password[129];
    char link_origin[65],link_article[65];
    char title[BOARD_TITLE+1], body[BOARD_BODY+1];
} OperationRequest;
typedef OperationRequest BoardRequest;
/* Operation effects are produced beside domain results, never decoded from text.
 * text remains the legacy display payload. Zero feedback means no successful effect. */
typedef struct {
    int failed,preferences_saved,checkpoint_saved,seen;
    char article_access;uint32_t article_access_id;int audit_failed;
    int page_found; uint32_t page_next;
    int file_detail,file_board_info,file_menu,file_unlocked,file_copied;
    int batch_match,mail_batch,file_children,file_guide,file_updated;
    uint32_t grant_board,grant_revision;
    unsigned count; uint32_t ids[16];
    unsigned child_count;uint32_t child_next;char children[16][49];
} OperationFeedback;
typedef OperationFeedback BoardFeedback;
typedef struct {
    int kind,state; /* 1=article, 2=mail; article state uses ArticleViewState */
    uint32_t id,parent,board,sender,recipient;uint64_t at;size_t size;
    char author[33],recipient_name[33],title[81];
} OperationContent;
typedef OperationContent BoardContent;
/* content metadata describes the authorized payload in draft_body. */
typedef struct { int structured;OperationPage page;BoardContent content; BoardFeedback feedback; uint64_t since,until;MailAttachments mail_attachments; unsigned long session; int slot; uint32_t selected, board; int auth, editing; uint32_t user_epoch,account_user,account_epoch; unsigned long chat_target; int zcontrol; int service_action; uint32_t service_number,service_next; char service_text[513]; int mail_notify_deny; uint32_t mail_notify_user,mail_notify_revision; int mail_action; uint32_t mail_id,mail_to,mail_parent,mail_next,mail_notice,mail_revision,mail_draft_notice,mail_delivery,mail_attachment; uint32_t account_user2,account_epoch2; uint32_t board_grant,board_revision; int board_refresh,access_refresh,sigop; int chat_admin; int chat_action; uint32_t chat_user,chat_kick_user; unsigned chat_kick_count; ChatPreferences chat_preferences; char chat_name[81],chat_login[33]; int birthday;int revoked, password_changed, preferences, language, pager; int menu_mode;int all_accounts_changed;int member_level;unsigned ui_aux;int editor_preference;unsigned columns,rows;int direct, sequence, deferred, binary_enter; unsigned binary_size; uint32_t sequence_next; int draft_loaded; uint32_t saved_draft_notice, unread_articles_notice, unread_boards_notice; uint32_t draft_revision,draft_parent; char draft_title[81],draft_body[CONTENT_BODY+1]; char text[BOARD_REPLY]; } OperationResult;
typedef OperationResult BoardResult;
#endif
