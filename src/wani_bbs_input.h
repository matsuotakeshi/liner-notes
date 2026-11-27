#ifndef LINER_WANI_BBS_INPUT_H
#define LINER_WANI_BBS_INPUT_H

#include <stdint.h>

typedef struct { char *word,*argument; } WaniCommand;
WaniCommand wani_command(char *);
int wani_equal(const char *,const char *);
int wani_match(const char *pattern,const char *name);
int wani_range(const char *,uint32_t current,uint32_t last,uint32_t *first,uint32_t *last_in_range);

#endif
