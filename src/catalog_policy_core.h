#ifndef CATALOG_POLICY_CORE_H
#define CATALOG_POLICY_CORE_H
#include "catalog_core.h"
#include "board_policy.h"
#include "board_policy_repository.h"
/* Internal single-worker services. Input/output storage must not overlap.
 * Owned snapshots and cleared failures; no prompt, editor, grants or refresh
 * mutation. Password input is borrowed only for the call and never returned.
 * Successful writes retain the existing format-upgrade and journal sequence. */
CatalogOutcome catalog_policy_get(const CatalogActor *,uint32_t,BoardPolicySettings *);
CatalogOutcome catalog_policy_set(const CatalogActor *,uint32_t,const char *specification);
CatalogOutcome catalog_password_prepare(const CatalogActor *,uint32_t);
CatalogOutcome catalog_password_set(const CatalogActor *,uint32_t,const char *password);
CatalogOutcome catalog_information_get(const CatalogActor *,uint32_t,char *body,size_t capacity);
/* attempted=1 only after authorization and format preparation, so the UI can
 * retain an editor after a write/text error without reopening a denied editor. */
CatalogOutcome catalog_information_set(const CatalogActor *,uint32_t,const char *body,int *attempted);
typedef struct { unsigned count;uint32_t next;BoardAccessRecord rows[8]; } BoardAccessPage;
/* Descending legacy cursor; up to eight rows/256 display candidates. The
 * retained lower bound follows current per-board policy (zero means no logs).
 * Audit metadata may refer to concealed articles; body/title is never read. */
CatalogOutcome catalog_access_page(const CatalogActor *,uint32_t,uint32_t cursor,BoardAccessPage *);
#endif
