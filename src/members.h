#ifndef MEMBERS_H
#define MEMBERS_H
#include <stdint.h>
#include "content_limits.h"
#include "chat_preferences.h"
typedef struct { uint32_t board,parent,revision; char title[81],body[CONTENT_BODY+1]; } MemberDraft;
int members_draft(uint32_t user,MemberDraft *draft);
/* 1 success, 0 version conflict, -1 storage/invalid input. */
int members_store_draft(uint32_t user,const MemberDraft *draft,uint32_t expected,int remove, uint32_t *revision);
int members_open(const char *path);
void members_close(void);
void members_registration_form(int);
int members_detailed_form(void);
void members_id_mode(int);
int members_auto_ids(void);
int members_id_format(const char *,unsigned);
uint32_t members_find(const char *);
uint32_t members_count(void);
int members_first_id(const char *);
extern int members_rtbbs_policy;
int members_enabled(void);
int members_profile_marker(uint32_t user);
int members_preferences(uint32_t user,int *language,int *pager);
int members_save_preferences(uint32_t user,int language,int pager);
void members_chat_preferences(uint32_t,ChatPreferences *);
/* 1 saved/no change, 0 invalid target/capacity, -1 storage failure. */
int members_chat_control(uint32_t,const char *);
void members_mail_notify(uint32_t user,int *deny,uint32_t *revision);
int members_set_mail_notify(uint32_t user,int deny);
int members_broken(void);
int members_reset_password(uint32_t user,uint32_t actor,const char *replacement);
int members_password(uint32_t user,const char *current,const char *replacement);
int members_auth(const char *name,const char *password,int create);
const char *members_name(uint32_t id);
int members_seen(uint32_t user,uint32_t article);
int members_mark(uint32_t user,uint32_t article,int seen);
int members_level(uint32_t user);
uint32_t members_groups(uint32_t user);
uint32_t members_group_word(uint32_t user,unsigned word);
int members_in_group(uint32_t user,unsigned group);
int members_group_change(uint32_t user,uint32_t actor,unsigned group,int add);
int members_group_move(uint32_t actor,unsigned from,unsigned to);
void members_ui_preferences(uint32_t user,int *editor,unsigned *columns,unsigned *rows,unsigned *aux);
int members_save_ui_preferences(uint32_t user,int language,int pager,int editor,unsigned columns,unsigned rows,unsigned aux);
int members_pending(uint32_t user);
int members_active(uint32_t);
int members_deleted(uint32_t);
int members_remove(uint32_t,uint32_t,unsigned);
int members_compact(const char *output);
/* Durable rights updates: 1 changed, 2 unchanged, 0 invalid, -1 storage. */
int members_rights(uint32_t user,uint32_t actor,int level,uint32_t groups);
int members_approve(uint32_t user,uint32_t actor);
int members_registration_mode(void);
void members_registration(int mode); /* 0 open, 1 approval, 2 closed */
#endif
