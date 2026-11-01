#ifndef BOARD_POLICY_H
#define BOARD_POLICY_H
#include <stdint.h>
#include <stddef.h>
#define BOARD_INFO_MAX 8192
int policy_open(const char *base);
void policy_close(void);
int policy_broken(void);
void policy_context(const uint32_t *grants);
int policy_locked(uint32_t board,uint32_t user);
int policy_deleted(uint32_t board);
int policy_menu(uint32_t board);
uint32_t policy_order(uint32_t board);
uint32_t policy_log_keep(uint32_t board);
uint32_t policy_max_articles(uint32_t board);
uint64_t policy_max_bytes(uint32_t board);
const char *policy_info(uint32_t board);
int policy_options(uint32_t board,uint32_t actor,const char *text);
int policy_password(uint32_t board,uint32_t actor,const char *password);
uint32_t policy_unlock(uint32_t board,const char *password);
int policy_information(uint32_t board,uint32_t actor,const char *body);
int policy_delete(uint32_t board,uint32_t actor);
void policy_report(uint32_t board,char *out,size_t size);
#endif
