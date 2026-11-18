#ifndef LINER_EDITOR_COMMANDS_H
#define LINER_EDITOR_COMMANDS_H
#include "editor_core.h"
/* Parsing only. Caller normalizes command words, preserves content case and
 * enforces editing/authentication state. text borrows the terminated input. */
typedef enum { DRAFT_UNHANDLED,DRAFT_INVALID,DRAFT_LINES,DRAFT_MUTATION } DraftCommandKind;
typedef struct { DraftCommandKind kind;EditorOperation operation;uint32_t line;const char *text; } DraftCommand;
int editor_command_word(const char *,const char *);
DraftCommand editor_draft_command(const char *);
typedef enum { LINE_INVALID,LINE_HELP,LINE_SELECT,LINE_NEW,LINE_UPLOAD,
    LINE_APPEND,LINE_SEARCH,LINE_LIST,LINE_EDIT,LINE_INSERT,LINE_DELETE,LINE_QUOTE } LineCommandKind;
typedef struct { LineCommandKind kind;uint32_t first,last;const char *query; } LineCommand;
/* Handles idle line-editor commands only, after interactive confirmation,
 * slash-command and input-mode routing. count is from editor_inspect(). */
LineCommand editor_line_command(const char *,uint32_t count,uint32_t current);
#endif
