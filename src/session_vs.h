#ifndef LINER_SESSION_VS_H
#define LINER_SESSION_VS_H
#include "session_mmm_internal.h"
#include "vs_input.h"

typedef enum { VS_IDLE, VS_DIRECT_MENU, VS_POST_BOARD, VS_POST_PARENT, VS_READ_START, VS_MODE_MENU,
               VS_MODE_NEWS, VS_POINTER_MENU, VS_POINTER_DATE, VS_WITHDRAW_CONFIRM, VS_EDITOR_MENU,
               VS_EDITOR_REPLACE, VS_FINDER, VS_BATCH } VsInput;
typedef enum { VS_SELECT, VS_LIST, VS_GROUP } VsPageMode;
typedef struct {
    MmmUi common;
    VsInput input;
    VsPageMode page;
    VsDirect direct;
    uint32_t current,root,selector_root,root_cursor,group_root,continuation,vault_id,news_current,news_previous;
    uint32_t scope,boards[MMM_SCAN_MAX],board_at,board_count,targets_next,position_board;
    uint64_t since,until;
    int related,read_related,descending,scan,automatic,await_start,page_backwards;
    int restore_mode,quiet,manual_list,posting,next_display,edit_line,filtered_list,scan_cancelled;
    char author[33];
    ChatEvent telegrams[4];
    unsigned telegram_count;
    uint32_t telegram_user,telegram_epoch;
    ChatPresenceRow peers[CONNECTION_MAX];
    unsigned peer_count;
    ConferenceAction topic_action;
    uint32_t topic_next;
} VsUi;
static inline int vs_active(const Session *s) { return s->ui_binding.active==session_ui_vs(); }
void vs_enter(Session *);
int vs_prompt(Session *);
void vs_menu(Session *,int);
void vs_help(Session *,const char *);
void vs_line(Session *,char *);
int vs_editor(Session *,char *);
int vs_ready(const Session *);
void vs_board(Session *);
void vs_result(Session *,MmmJob,const OperationResult *,SessionResultView);
int vs_defer_telegram(Session *,const ChatEvent *);
void vs_tick(Session *);
int vs_automatic(const Session *);
int vs_scan_cancelled(Session *);
void vs_scan_stop(Session *);
#endif
