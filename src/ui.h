#ifndef UI_H
#define UI_H
#include <stddef.h>
#include <stdint.h>
#include "messages.h"
enum { UI_HOME, UI_BOARDS, UI_NOTES, UI_TREE, UI_UNREAD, UI_ARTICLE, UI_LIST, UI_MEMBERS, UI_AUDIT, UI_SEARCH, UI_FIND, UI_PROFILES, UI_FILES, UI_FILEAUDIT, UI_FILECATEGORIES, UI_FILEBOARDS, UI_NEWS };
enum { UI_INPUT_NONE, UI_LOGIN, UI_REGISTER, UI_POST, UI_REPLY, UI_SEARCH_INPUT };
typedef struct {
    Language language;
    int enabled, view, input, file_mode, admin, sigop;
    uint32_t next, tree, unread_root;
    uint32_t audit_article,audit_actor;
    int next_after_seen, notes_unread;
    char file_query[130];
    char board_title[81], search[81], find_query[160], profile_query[81];
} Ui;
/* Formats already authorized backend output. Does not access stores or grant access. */
int ui_format(Ui *ui,const char *wire,char *out,size_t size);
void ui_prompt(const Ui *ui,uint32_t board,uint32_t article,const char *name,int editing,int password,char *out,size_t size);
const char *ui_menu(const Ui *ui);
const char *ui_text(const Ui *ui,MessageId id);
#endif
