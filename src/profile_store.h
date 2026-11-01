#ifndef PROFILE_STORE_H
#define PROFILE_STORE_H
#include <stdint.h>
#define PROFILE_FIELDS 13
#define PROFILE_BUFFER 13
int profile_store_open(const char *);
void profile_store_close(void);
int profile_store_broken(void);
const char *profile_key(unsigned);
int profile_field(const char *);
const char *profile_value(uint32_t,unsigned);
const char *profile_visible(uint32_t owner,uint32_t viewer,unsigned field,char scratch[16]);
int profile_admin_set(uint32_t,uint32_t,unsigned,const char *);
int profile_text_valid(unsigned,const char *);
int profile_set(uint32_t,unsigned,const char *);
int profile_visibility(uint32_t,unsigned,unsigned);
int profile_match(uint32_t owner,uint32_t viewer,int field,const char *query);
#endif
