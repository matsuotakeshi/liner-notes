#ifndef EDITOR_CORE_H
#define EDITOR_CORE_H
#include <stddef.h>
#include <stdint.h>
#include "content_limits.h"
#define EDITOR_TITLE 80
/* Session-owned buffers: title/undo_title[81], body/undo_body[CONTENT_BODY+1].
 * Synchronous and single-owner; all five storage regions must be disjoint.
 * Text arguments must not alias document storage. No persistence or authority
 * is granted here: publication must pass the existing authenticated Core. */
typedef struct { char *title,*body,*undo_title,*undo_body;int *undo_available; } EditorDocument;
typedef enum { EDITOR_OK,EDITOR_INVALID,EDITOR_LINE,EDITOR_TITLE_INVALID,
    EDITOR_CONTROL,EDITOR_LIMIT,EDITOR_NO_UNDO } EditorOutcome;
typedef enum { EDITOR_SET_TITLE,EDITOR_INSERT,EDITOR_REPLACE,EDITOR_DELETE,
    EDITOR_QUOTE,EDITOR_CLEAR,EDITOR_APPEND_LINE,EDITOR_APPEND_BLOCK,EDITOR_UNDO } EditorOperation;
typedef struct { size_t bytes;uint32_t lines; } EditorInfo;
/* Byte offsets exclude CRLF for a line, include CRLF for a range. */
typedef struct { const char *text;size_t size,offset; } EditorSpan;
typedef struct { size_t offset,size; } EditorMatch;
EditorOutcome editor_inspect(const EditorDocument *,EditorInfo *);
EditorOutcome editor_range(const EditorDocument *,uint32_t first,uint32_t last,EditorSpan *);
/* Iterate a previously inspected body without repeated whole-document scans.
 * 1=line, 0=end, -1=invalid. No mutation while spans/iterators are in use. */
int editor_next_line(const char *body,size_t bytes,size_t *cursor,EditorSpan *);
/* Literal, case-sensitive, nonoverlapping UTF-8 byte matches within one line.
 * cursor is a byte offset; 1=match, 0=end, -1=invalid/empty query. */
int editor_find_next(const EditorSpan *,const char *query,size_t *cursor,EditorMatch *);
EditorOutcome editor_apply(EditorDocument *,EditorOperation,uint32_t first,uint32_t last,const char *text);
void editor_discard_undo(EditorDocument *);
#endif
