#ifndef LINER_SESSION_NET_COCK_H
#define LINER_SESSION_NET_COCK_H

#include "session_mmm_internal.h"
#include "net_cock_input.h"

typedef enum {
    NC_IDLE, NC_BOARD_SELECT, NC_TITLE_EDIT, NC_BODY_EDIT,
    NC_DELETE, NC_BATCH_SELECT, NC_SUBSCRIPTION, NC_PASSWORD_OLD, NC_PASSWORD_NEW,
    NC_PASSWORD_CONFIRM, NC_MAIL_TO, NC_VAULT_UPLOAD
} NetCockInput;
typedef enum { NC_HEADERS, NC_LIST_NAMES, NC_LIST_IDS, NC_HEADER_BODY } NetCockPage;

typedef struct {
    MmmUi common;
    NetMenu menu;
    NetCockInput input;
    NetCockPage page;
    uint32_t selected, parent, last_read, history[10], list_cursor;
    unsigned history_count;
    uint32_t scan_boards[MMM_SCAN_MAX], scan_cursor;
    unsigned scan_count, scan_index;
    uint64_t since, until;
    int mode, write_board, scanning, continuous, close_after_batch, in_batch, restore_mode;
    int back_action, direction;
    uint32_t transfer_file, pending_history, topic_next, subscription_board;
    ConferenceAction topic_mode;
    char old_password[129], new_password[129];
    char upload_description[241];
    ChatPresenceRow peers[CONNECTION_MAX];
    unsigned peer_count;
} NetCockUi;

static inline int net_cock_active(const Session *s) { return s->ui_binding.active==session_ui_net_cock(); }
int net_cock_prompt(Session *);
void net_cock_help(Session *,const char *);
void net_cock_menu(Session *,int);
void net_cock_line(Session *,char *);
int net_cock_secret(const Session *);
int net_cock_ready(const Session *);
int net_cock_editor(Session *,char *);
void net_cock_board(Session *);
void net_cock_boards(Session *,const ArticleBoardsPage *);
void net_cock_result(Session *,MmmJob,const OperationResult *,SessionResultView);
void net_cock_login(Session *,const OperationResult *);
void net_cock_tick(Session *);
int net_cock_keys(const Session *);
void net_cock_key(Session *,unsigned char);
void net_cock_transfer_finished(Session *,int);
#endif
