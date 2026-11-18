#ifndef AUTHENTICATION_FORM_H
#define AUTHENTICATION_FORM_H
#include "authentication_core.h"
/* Legacy adapter only. Decodes FIELD|value\n into borrowed typed entries.
 * Every valid legacy field uses >=5 bytes, so 2048 entries cover the 8192-byte
 * input limit without discarding duplicates. Keep this owner alive for Core. */
typedef struct {
    AuthenticationForm form;
    AuthenticationField fields[AUTH_FORM_FIELDS];
    char text[AUTH_FORM_BYTES+1];
} AuthenticationDecodedForm;
void authentication_form_decode(const char *,AuthenticationDecodedForm *);
#endif
