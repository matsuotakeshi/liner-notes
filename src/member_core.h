#ifndef MEMBER_CORE_H
#define MEMBER_CORE_H
#include <stdint.h>

/* Internal, worker-owned account query service. Trusted session supplies its
 * authenticated ID/epoch; no caller admin flag or cached authorization. No
 * socket/UI dependency or writes. Outputs own their data and clear on failure.
 * Input/output storage must not overlap; no pointer is retained. */
typedef struct { uint32_t user,epoch; } MemberActor;
typedef enum { MEMBER_CORE_OK, MEMBER_CORE_REVOKED, MEMBER_CORE_STORAGE,
               MEMBER_CORE_MODERATION_STORAGE, MEMBER_CORE_ADMIN_REQUIRED,
               MEMBER_CORE_NOT_FOUND, MEMBER_CORE_INVALID } MemberOutcome;
typedef enum { MEMBER_ROLE_NONE, MEMBER_ROLE_GUEST, MEMBER_ROLE_MEMBER, MEMBER_ROLE_ADMIN } MemberRole;
typedef enum { MEMBER_STATE_ACTIVE, MEMBER_STATE_PENDING, MEMBER_STATE_SUSPENDED } MemberState;
typedef struct { uint32_t id; char name[33]; MemberState state; } MemberSummary;
typedef struct { uint32_t next; unsigned count; MemberSummary rows[8]; } MemberPage;

/* Own role only, including a guest. NONE on error is never a guest success. */
MemberOutcome member_core_role(const MemberActor *,MemberRole *out);
/* Administrator-only directory/status. start=0 means 1, otherwise inclusive;
 * next is the first undisplayed existing ID. Beyond-end pages remain empty.
 * Preserve stored member names and pending-before-suspended state precedence;
 * these summaries do not assert login eligibility or expose private profiles. */
MemberOutcome member_core_list(const MemberActor *,uint32_t start,MemberPage *out);
MemberOutcome member_core_get(const MemberActor *,uint32_t target,MemberSummary *out);
#endif
