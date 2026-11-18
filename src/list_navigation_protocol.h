#ifndef LIST_NAVIGATION_PROTOCOL_H
#define LIST_NAVIGATION_PROTOCOL_H
#include <stddef.h>
#include "list_navigation_core.h"
typedef struct { int found;uint32_t next; } ListContinuation;
/* Trusted worker text only. Preserve existing marker priority and scanf prefix
 * handling; not a parser or authorization check for arbitrary client commands. */
ListContinuation list_continuation_parse(const char *,int allow_text);
int report_navigation_command(const ReportNavigation *,char *out,size_t capacity);
void vault_children_response(VaultNavigation *,const char *text);
#endif
