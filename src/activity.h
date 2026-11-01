#ifndef ACTIVITY_H
#define ACTIVITY_H
#include "board.h"
extern unsigned gather_limit;
int activity_open(const char *base);
void activity_close(void);
void activity_execute(const BoardRequest *,BoardResult *);
int gather_line_valid(const char *);
#endif
