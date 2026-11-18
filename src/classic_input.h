#ifndef LINER_CLASSIC_INPUT_H
#define LINER_CLASSIC_INPUT_H
#include <stddef.h>
/* Snapshot of terminal interaction state, never an authorization decision. */
typedef struct {
    int memo,selection,application,classic,secret,input,mail_input,paging,binary,transfer;
    int editing,chatting,gather,talk,line_editor,editor_input,mail_mode;
} ClassicInput;
/* line[0..size) is an unterminated prefix, called BEFORE receiving the next byte. */
int classic_input_uppercase(const ClassicInput *,const char *,size_t);
/* Mutable NUL-terminated line, size excludes NUL. Preserve data arguments. */
void classic_input_normalize(const ClassicInput *,char *,size_t);
#endif
