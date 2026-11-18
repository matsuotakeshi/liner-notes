#ifndef TRAVERSAL_PROTOCOL_H
#define TRAVERSAL_PROTOCOL_H
#include "traversal_core.h"
/* Existing CHECK argument and trusted worker marker grammar only; no ACL. */
unsigned check_traversal_select(CheckTraversal *,const char *kind,uint32_t board);
void check_traversal_response(CheckTraversal *,const char *text);
#endif
