#ifndef MODERATION_H
#define MODERATION_H
#include <stdint.h>
/* Immutable startup roles; all APIs owned by the board worker after startup. */
typedef struct {
    uint32_t sequence,target,board,actor;
    uint64_t time;
    char kind;
    int enabled;
} ModerationEntry;
uint32_t moderation_count(void);
/* Reads one committed entry without retaining history in memory. Worker only. */
int moderation_entry(uint32_t sequence,ModerationEntry *entry);
int moderation_admins(const char *path);
int moderation_admin(uint32_t user);
int moderation_open(const char *board_path,uint32_t count,uint32_t (*board_of)(uint32_t),int (*was_on)(uint32_t,uint32_t));
void moderation_close(void);
int moderation_broken(void);
int moderation_protected(uint32_t user);
int moderation_chat_kick(uint32_t user,uint32_t actor);
int moderation_suspended(uint32_t user);
uint32_t moderation_epoch(uint32_t user);
void moderation_credentials_changed(uint32_t user);
int moderation_chat_kick(uint32_t user,uint32_t actor);
int moderation_suspend(uint32_t user,uint32_t actor,int suspend);
int moderation_hidden(uint32_t article);
/* Append + fsync before publishing state; failures disable board access. */
int moderation_set(uint32_t article,uint32_t board,uint32_t actor,int hidden);
#endif
