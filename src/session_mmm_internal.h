#ifndef LINER_SESSION_MMM_INTERNAL_H
#define LINER_SESSION_MMM_INTERNAL_H
#include "session_io.h"
#include "form_flow_core.h"
#include "batch_core.h"

typedef enum {
    MAIN_SCREEN, NOTES_SCREEN, BOARD_SCREEN, INDEX_SCREEN, OPEN_SCREEN, MAIL_SCREEN,
    FILER_SCREEN, CHAT_SCREEN, TERM_SCREEN, GAMES_SCREEN, NEW_SCREEN
} MmmScreen;
typedef enum {
    INPUT_NONE, INPUT_LOGIN, INPUT_PASSWORD, INPUT_BOARD_PASSWORD, INPUT_TITLE,
    INPUT_BODY, INPUT_EDITOR, INPUT_NOTE, INPUT_RESPONSE, INPUT_FIND, INPUT_FILE_PASSWORD
} MmmPrompt;
typedef enum {
    JOB_NONE, JOB_AUTH, JOB_BOARDS, JOB_NAMED, JOB_BOARD, JOB_UNLOCK, JOB_INDEX,
    JOB_BROWSE, JOB_READ, JOB_SEEN, JOB_PREPARE, JOB_POST, JOB_MAIL, JOB_FILES,
    JOB_FILE_UNLOCK, JOB_FILE_ADD, JOB_CHAT, JOB_PREF, JOB_PROFILE, JOB_GAME, JOB_TRANSFER,
    JOB_MAIL_BATCH, JOB_SEQUENCE, JOB_NEW_BEGIN, JOB_NEW_PAGE, JOB_NEW_READ, JOB_OFF,
    JOB_SCANLIST, JOB_SCANLIST_SAVE, JOB_LABEL, JOB_OPEN_WINDOW, JOB_OPEN_PAGE, JOB_OPEN_READ
} MmmJob;
typedef enum { OPEN_BOARD_MENU, OPEN_INDEX, OPEN_BASE, OPEN_POST, OPEN_NEW, OPEN_NEW_OR_INDEX } MmmBoardAction;
typedef enum { BROWSE_READ, BROWSE_INDEX, BROWSE_EXACT, BROWSE_RESPONSE, BROWSE_TITLES } MmmBrowse;

typedef struct {
    Language language;
    MmmScreen screen;
    MmmPrompt input;
    MmmJob job;
    MmmBoardAction action;
    MmmBrowse browse;
    ApplicationForm application;
    char auth_op, login[33];
    uint32_t parent, ancestors[1024];
    unsigned depth;
    uint32_t board_target, lock, target, next, page_start, rows[8];
    unsigned row_count;
    uint32_t root, article, reply_number, browse_root, number_seen, search_next;
    int descending, unread, search_responses;
    char search[81], board_title[81];
    uint32_t post_parent, insert_at;
    int numbered, undo;
    unsigned first_digit;
    char title[81], undo_title[81], body[CONTENT_BODY+1], undo_body[CONTENT_BODY+1];
    uint32_t mail_to, mail_parent, mail_revision, service_next, file_text_id;
    MailAttachments attachments;
    MailListMode mail_mode;
    int file_action, chatting, chat_kicked;
    char file_path[49], requested_path[49], file_filter[81];
    BatchSelection files;
    int pager, editor;
    unsigned ui_aux;
    uint64_t login_at, new_since, new_until;
    int new_all, sequence_read;
    int scan_custom;
    unsigned scan_count,scan_at;
    uint32_t scan_boards[MMM_SCAN_MAX],open_board;
} MmmUi;

MmmUi *mmm_state(Session *s);
const MmmUi *mmm_view(const Session *s);
const char *mmm_local(const Session *s,const char *en,const char *ja);
void mmm_say(Session *s,const char *en,const char *ja);
void mmm_prompt(Session *s);
void mmm_menu(Session *s,int requested);
OperationRequest *mmm_request(Session *s,char op);
int mmm_submit(Session *s,OperationRequest *q,MmmJob job);
void mmm_clear_composition(MmmUi *m);
int mmm_ui_text_valid(const char *text,size_t limit);
void mmm_bad_service_command(Session *s);
void mmm_simple_operation(Session *s,char op,int action,uint32_t number,MmmJob job);
void mmm_mail_list(Session *s,MailListMode mode,uint32_t cursor);
void mmm_file_enter(Session *s,const char *path);
void mmm_profile(Session *s,const char *arg);
void mmm_chat_request(Session *s,int action,unsigned long target,const char *text);
void mmm_games(Session *s,uint32_t id);
void mmm_new_page(Session *s,uint32_t cursor);
void mmm_open_page(Session *s,uint32_t cursor);
void mmm_off(Session *s,int skip);
int mmm_service_line(Session *s,char *text);
void mmm_mail_result(Session *s,const OperationResult *r,SessionResultView projection);
void mmm_mail_batch_result(Session *s,const OperationFeedback *feedback);
void mmm_file_result(Session *s,const OperationResult *r,const OperationPage *page,int add);
int mmm_chat_event(Session *s,const ChatEvent *e);
void mmm_chat_result(Session *s,const OperationResult *r,uint64_t now);
void mmm_load_preferences(Session *s,const OperationResult *r);
void mmm_settings_summary(Session *s);
void mmm_render_profile(Session *s,const OperationPage *page);
void mmm_door_result(Session *s,const OperationResult *r,const OperationPage *page);
void mmm_render_news(Session *s,const ArticlePage *page);
#endif
