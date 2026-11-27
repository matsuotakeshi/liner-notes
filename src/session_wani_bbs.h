#ifndef LINER_SESSION_WANI_BBS_H
#define LINER_SESSION_WANI_BBS_H

#include "session_mmm_internal.h"
#include "wani_core.h"

typedef enum { WANI_HOME_BOARD, WANI_INDEX, WANI_PAGES, WANI_ARTICLE } WaniView;
typedef struct {
    MmmUi common;
    WaniView view,back_from_read;
    uint32_t mode,root,article,parent,row_ids[8],page_ordinal,range_end;
    unsigned row_count;
    int opening,read_first,descending,unread,range_active,returning,password_step,topic_step;
    int traversal, scan_kind, scan_mark, topic_latest, editor_cancel;
    ConferenceAction topic_mode;
    uint32_t topic_next;
    uint64_t since,until;
    uint32_t scan[MMM_SCAN_MAX];
    unsigned scan_count,scan_at;
    char author[33],reply_title[81],query[81];
    char old_password[129],new_password[129];
} WaniUi;

static inline int wani_active(const Session *s) {
    return s->ui_binding.active==session_ui_wani_bbs();
}
int wani_prompt(Session *);
void wani_menu(Session *,int);
void wani_help(Session *,const char *);
void wani_line(Session *,char *);
int wani_board_enter(Session *);
void wani_result(Session *,MmmJob,const OperationResult *,SessionResultView);
void wani_render_article(Session *,const OperationContent *);
int wani_editor_line(Session *,char **);
int wani_secret(const Session *);
int wani_ready(const Session *);
int wani_store_personal(Session *,const char *,size_t);

#endif
