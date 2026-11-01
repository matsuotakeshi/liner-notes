#ifndef PRINCIPAL_H
#define PRINCIPAL_H
#include <stdint.h>
/* Authentication-provider output, never a credential or a client assertion.
 * Domain-prefixed subject survives username changes and separates ID spaces. */
typedef struct { char subject[80]; uint32_t capabilities; unsigned level; uint32_t groups; uint32_t group_high[3]; uint32_t vault_grants[1025]; } Principal;
enum { CAP_VAULT_READ=1u, CAP_VAULT_MANAGE=2u, CAP_VAULT_UPLOAD=4u };
typedef struct {
    void *context;
    int (*resolve)(void *context,uint32_t session_user,uint32_t epoch,Principal *out);
} IdentityProvider;
#endif
