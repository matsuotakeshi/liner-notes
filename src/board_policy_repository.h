#ifndef BOARD_POLICY_REPOSITORY_H
#define BOARD_POLICY_REPOSITORY_H
#include <stdint.h>
/* Worker-owned persistence boundary. No Session or terminal state. */
typedef struct {
    uint32_t sequence,board,article,user;uint64_t time;char kind;
} BoardAccessRecord;
int board_policy_repository_broken(void);
int board_policy_repository_prepare(void);
int board_policy_repository_has_articles(uint32_t board);
/* On failure marks access storage broken; never reports successful refresh. */
int board_policy_repository_prune(void);
uint32_t board_policy_repository_access_count(void);
int board_policy_repository_access_get(uint32_t index,BoardAccessRecord *out);
#endif
