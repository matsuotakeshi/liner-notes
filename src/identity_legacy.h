#ifndef IDENTITY_LEGACY_H
#define IDENTITY_LEGACY_H
#include "principal.h"
/* Existing account-store adapter. Call on its owner worker only. */
IdentityProvider identity_legacy_provider(void);
#endif
