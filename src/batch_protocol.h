#ifndef LINER_BATCH_PROTOCOL_H
#define LINER_BATCH_PROTOCOL_H
#include "batch_core.h"
/* Parses trusted worker response suffixes (space-prefixed IDs), preserving the
 * existing valid-prefix semantics and 16-ID cap. Not user command validation. */
BatchIds batch_response_ids(const char *);
/* Empty ID list produces empty text; no partial text on capacity failure. */
int batch_format_ids(const BatchIds *,char *,size_t);
/* Existing helper receipt syntax; caller MUST validate connection/generation. */
int batch_download_receipt(const char *,uint32_t *);
#endif
