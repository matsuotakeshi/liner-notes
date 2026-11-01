#ifndef EXTRAS_H
#define EXTRAS_H
#include <stdint.h>
#include "moderation.h"
uint32_t extras_audit_count(void);
/* Resolve a combined history number, or return its moderation sequence. */
int extras_audit_entry(uint32_t sequence,ModerationEntry *entry,uint32_t *moderation_sequence);
int extras_open(const char *path,uint32_t articles);
void extras_close(void);
int extras_broken(void);
int extras_descriptor(void);
int extras_removed(uint32_t id);
const char *extras_title(uint32_t id);
const char *extras_author(uint32_t id);
const char *extras_handle(uint32_t user);
const char *extras_bio(uint32_t user);
/* Worker-only append journal; acknowledge only after sync. */
const char *extras_field(uint32_t user,unsigned field);
int extras_profile_match(uint32_t user,const char *query);
int extras_set(char kind,uint32_t id,uint32_t actor,const char *text);
#endif
