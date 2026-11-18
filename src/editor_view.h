#ifndef LINER_EDITOR_VIEW_H
#define LINER_EDITOR_VIEW_H
#include "editor_core.h"
typedef enum { EDITOR_VIEW_OK,EDITOR_VIEW_INVALID,EDITOR_VIEW_CAPACITY,
    EDITOR_VIEW_MEMORY,EDITOR_VIEW_OUTPUT } EditorViewResult;
/* Read-only presentation over Editor Core. Output is NUL-terminated or empty
 * on failure. Document must not overlap output or mutate during the call. */
EditorViewResult editor_view_draft(const EditorDocument *,char *,size_t);
EditorViewResult editor_view_lines(const EditorDocument *,uint32_t,uint32_t,int,char *,size_t);
/* Callback receives one complete matching line, borrowed only for the call;
 * return 0 to stop after transport failure. Search counts occurrences, not lines.
 * No matching bytes or title are sent until the whole document/range is valid.
 * No paging, language, Session, authentication, persistence or UI state here. */
typedef int (*EditorViewWrite)(void *,const char *);
EditorViewResult editor_view_search(const EditorDocument *,uint32_t,uint32_t,const char *,int,int,EditorViewWrite,void *,unsigned *);
#endif
