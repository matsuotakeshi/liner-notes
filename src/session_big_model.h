#ifndef LINER_SESSION_BIG_MODEL_H
#define LINER_SESSION_BIG_MODEL_H

#include "session_mmm_internal.h"

typedef enum {
    BIG_MODEL_READY, BIG_MODEL_DESTINATION, BIG_MODEL_TELEGRAM, BIG_MODEL_TERM_DATE,
    BIG_MODEL_OLD_PASSWORD, BIG_MODEL_NEW_PASSWORD, BIG_MODEL_CONFIRM_PASSWORD
} BigModelInput;
typedef enum {
    BIG_MODEL_BOARD, BIG_MODEL_FIND, BIG_MODEL_NEWS
} BigModelList;

typedef struct {
    MmmUi shared;
    BigModelInput input;
    BigModelList list;
    ArticleScope scope;
    MmmScreen article_return, news_return;
    ChatPresenceRow recipients[CONNECTION_MAX], destination;
    unsigned recipient_count;
    uint64_t since, until, override_since;
    uint32_t news_board, news_cursor;
    int override_date, unread, quiet, news_suspended;
    char old_password[129], new_password[129];
    char query[81];
    char *page_body;
    size_t page_offset;
    uint32_t page_id, page_board;
} BigModelUi;

static inline int big_model_active(const Session *s) {
    return s->ui_binding.active==session_ui_big_model();
}
int big_model_ready(const Session *s);
int big_model_secret(const Session *s);
int big_model_prompt(Session *s);
void big_model_menu(Session *s,int requested);
void big_model_help(Session *s,const char *topic);
void big_model_line(Session *s,char *line);
int big_model_keys(const Session *s);
void big_model_key(Session *s,unsigned char ch);
void big_model_articles(Session *s,const ArticlePage *page,int read_first);
void big_model_header(Session *s,uint32_t id,uint64_t timestamp,const char *author,const char *title);
void big_model_window(Session *s,uint64_t since,uint64_t until,const MmmScanList *list);
void big_model_scanlist(Session *s,const MmmScanList *list);
int big_model_article_body(Session *s,const OperationContent *article,const char *body);
void big_model_clear_page(Session *s);
void big_model_destinations(Session *s);

#endif
