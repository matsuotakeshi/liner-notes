#ifndef APPLICATION_REPOSITORY_H
#define APPLICATION_REPOSITORY_H
#include <stdint.h>

/* Internal single-owner board store, not a UI/transport API. Core authorizes
 * application review; registration persists notes only after account creation.
 * No connection or input pointer is retained. Missing and present-empty notes
 * are distinct. load clears its owned output on any failed/partial read.
 * Existing applications table and its transaction/sync behavior are retained. */
typedef enum { APPLICATION_FROM_BBS, APPLICATION_SYSOP_NOTE } ApplicationNoteField;
typedef struct { unsigned char present[2]; char values[2][321]; } ApplicationNotes;
int application_repository_load(uint32_t user,ApplicationNotes *out);
int application_repository_save(uint32_t user,ApplicationNoteField field,const char *value);
#endif
