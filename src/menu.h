#ifndef MENU_H
#define MENU_H
#include "messages.h"
/* Startup-only immutable UTF-8 menus. Missing default files use built-in text. */
int menu_load(const char *directory,int required);
const char *menu_text(Language language);
const char *menu_section_text(Language,unsigned);
int help_load(const char *directory,int required);
const char *help_text(Language,const char *topic);
int banner_load(const char *path);
const char *banner_text(void);
int login_text_load(const char *);
const char *login_text(unsigned);
const char *hourly_greeting(unsigned hour);
int site_load(const char *,const char *);
const char *site_text(unsigned);
#endif
