#ifndef ACTIVITY_REPOSITORY_H
#define ACTIVITY_REPOSITORY_H
#include "activity_core.h"
/* Internal serialized storage API. Only Core may admit live operations.
 * Lifecycle/replay are startup/shutdown only, with no live worker requests. */
typedef struct { uint32_t sequence,actor;uint64_t time;char body[ACTIVITY_BODY+1]; } ActivityRecord;
int activity_repository_ready(void);
unsigned activity_repository_limit(void);
unsigned activity_repository_count(void);
int activity_repository_read(unsigned,ActivityRecord *);
int activity_repository_append(char kind,uint32_t actor,uint32_t target,const char *);
#endif
