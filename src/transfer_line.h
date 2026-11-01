#ifndef TRANSFER_LINE_H
#define TRANSFER_LINE_H
#include "transfer.h"
/* line-v1 connector. Encoding belongs here, never in the storage API.
 * 672 bytes leave space for command/ID/offset within a 1024-byte input line. */
#define TRANSFER_LINE_CHUNK 672
int transfer_line_command(const char *);
void transfer_line_execute(TransferBackend,const Principal *,const char *,char *,size_t);
#endif
