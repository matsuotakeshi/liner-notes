#ifndef LINER_TERMINAL_PAGER_H
#define LINER_TERMINAL_PAGER_H
#include <stddef.h>
typedef struct { unsigned columns,rows; } TerminalDimensions;
TerminalDimensions terminal_dimensions(unsigned,unsigned);
/* Source is an already authorized, formatted, NUL-terminated response.
 * offset is in [0,strlen(source)] and belongs to this source. No overlapping buffers.
 * Returns -1 for capacity <5 (no cursor change), 0 complete, 1 more.
 * Does not own text, clear secrets, produce prompts or change a reading cursor. */
int terminal_pager_render(const char *,size_t *,unsigned,unsigned,char *,size_t);
/* Legacy RTBBS response selection belongs to presentation, not data access. */
int terminal_pager_response(const char *);
#endif
