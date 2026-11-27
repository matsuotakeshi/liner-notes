#ifndef LINER_VS_INPUT_H
#define LINER_VS_INPUT_H
#include <stdint.h>

/* Only command tokens are folded. The remaining line is caller-owned text. */
typedef struct { char key[16];char *argument; } VsCommand;
typedef enum { VS_DIRECT_INVALID, VS_DIRECT_BASE, VS_DIRECT_REPLY, VS_DIRECT_JUMP } VsDirectKind;
typedef struct { VsDirectKind kind;char board[81],title[81];uint32_t parent; } VsDirect;
VsCommand vs_command(char *);
VsDirect vs_direct(const char *);
#endif
