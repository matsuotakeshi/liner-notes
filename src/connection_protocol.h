#ifndef LINER_CONNECTION_PROTOCOL_H
#define LINER_CONNECTION_PROTOCOL_H
#include "connection_core.h"
/* Existing administrative RAW response; all-or-empty on insufficient space. */
int connection_protocol_list(const ConnectionSummary *,size_t,char *,size_t);
#endif
