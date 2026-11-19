#ifndef BOARD_H
#define BOARD_H
#include "operation.h"
/* Worker lifecycle and queue implementation; UI-free contracts are in operation.h. */
int board_start(const char *path,const char *members,const char *catalog);
int board_link_config(const char *path);
int board_link_enabled(uint32_t board,uint32_t user);
int board_start_config(const char *path,const char *members,const char *catalog,const char *admins);
int board_save_disconnect(const BoardRequest *);
int board_submit(const BoardRequest *request);
int board_poll(BoardResult *result);
int board_completion_fd(void);
void board_completion_drain(void);
void board_completion_notify(void);
void board_stop(void);
int board_disable_feature(const char *name);
int board_feature_enabled(const char *name);
#endif
