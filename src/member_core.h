#ifndef MEMBER_CORE_H
#define MEMBER_CORE_H
#include <stdint.h>

/* Internal, worker-owned account service. Trusted session supplies its
 * authenticated ID/epoch; no caller admin flag or cached authorization. No
 * socket/UI dependency. Outputs own their data and clear on failure.
 * Input/output storage must not overlap; no pointer is retained. */
typedef struct { uint32_t user,epoch; } MemberActor;
typedef enum { MEMBER_CORE_OK, MEMBER_CORE_REVOKED, MEMBER_CORE_STORAGE,
               MEMBER_CORE_MODERATION_STORAGE, MEMBER_CORE_ADMIN_REQUIRED,
               MEMBER_CORE_NOT_FOUND, MEMBER_CORE_INVALID, MEMBER_CORE_PROTECTED,
               MEMBER_CORE_EXTRAS_STORAGE, MEMBER_CORE_MODERATION_UNCERTAIN,
               MEMBER_CORE_LOGIN_REQUIRED, MEMBER_CORE_RIGHTS_INVALID, MEMBER_CORE_STATE, MEMBER_CORE_PASSWORD, MEMBER_CORE_PREFERENCES } MemberOutcome;
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
/* Admin-only durable suspend/resume. suspend must be 0 or 1. Self/protected
 * targets are rejected, and combined audit capacity is checked before append.
 * Existing store owns idempotence, audit publication and credential epochs.
 * On success forward the committed target/epoch to existing connection
 * invalidation. No success data is returned on failure; no socket is touched.
 * Resume clears the suspension flag, not pending/deleted-account restrictions. */
typedef struct { uint32_t user,epoch; } MemberChange;
MemberOutcome member_core_suspend(const MemberActor *,uint32_t target,int suspend,MemberChange *out);
/* Admin-only pending applications, with the same inclusive paging contract.
 * Guests receive LOGIN_REQUIRED; only active pending accounts are returned. */
MemberOutcome member_core_applications(const MemberActor *,uint32_t start,MemberPage *out);
/* Approve an active pending account. The member store owns durable publication;
 * Core advances the credential epoch only after a changed result. Repeated
 * approval is a state error, not a successful no-op. Forward MemberChange via
 * the existing runtime notification path. No other rights are modified. */
MemberOutcome member_core_approve(const MemberActor *,uint32_t target,MemberChange *out);
/* Own rights, or another active account for a global administrator. target is
 * explicit (not zero-as-self). Group indexes 0..99 represent groups 1..100.
 * This snapshot is descriptive, never an authorization token. */
typedef struct { uint32_t user; int level; unsigned char groups[100]; MemberState state; } MemberRights;
MemberOutcome member_core_rights(const MemberActor *,uint32_t target,MemberRights *out);
/* Global admin only. Preserve all group memberships and pending/suspended
 * state. Same level succeeds with a zero change result and no epoch update;
 * changed results advance the epoch once, after durable member-store success. */
MemberOutcome member_core_level(const MemberActor *,uint32_t target,unsigned level,MemberChange *out);
/* GROUP/UNGROUP on the caller's board context. Global admins may manage any
 * active target; a SIGOP needs catalog authority on this board and a group in
 * this board's ACL, and cannot modify protected accounts. Rechecked per call,
 * no caller-provided delegation flag. add must be 0/1, group 1..100.
 * Only changed durable writes publish a new epoch; no-ops return zero change. */
MemberOutcome member_core_group(const MemberActor *,uint32_t board,uint32_t target,unsigned group,int add,MemberChange *out);
/* Durable terminal account transitions, retaining IDs/names. Only RETIRE is
 * available to a non-admin and always targets the authenticated actor (the
 * supplied target is ignored). Other kinds require global admin authority.
 * Protected accounts cannot be removed; admins cannot delete themselves via
 * non-retirement operations. Store validates pending/nonpending state.
 * Successful completion advances one epoch and returns its notification. */
typedef enum { MEMBER_REJECT=1, MEMBER_DELETE_APPLICATION=2,
               MEMBER_RETIRE=3, MEMBER_DELETE=4 } MemberRemoval;
MemberOutcome member_core_remove(const MemberActor *,uint32_t target,unsigned kind,MemberChange *out);
/* Reset prompt authorization is read-only, and is repeated on commit. Existing
 * name/protected-target policy is preserved; the store owns target eligibility.
 * Password arguments are borrowed NUL-terminated strings, never copied/retained
 * or logged here. Caller owns secure cleanup on every path; output cannot alias
 * inputs. Store retains current-password verification, hashing and durable sync.
 * Only success publishes an epoch. Own-password caller keeps its connection via
 * the existing adapter flag; reset revokes the target's old connections. */
MemberOutcome member_core_reset_check(const MemberActor *,uint32_t target);
MemberOutcome member_core_reset_password(const MemberActor *,uint32_t target,const char *replacement,MemberChange *out);
MemberOutcome member_core_password(const MemberActor *,const char *current,const char *replacement,MemberChange *out);
/* Actor-owned display preferences; no target parameter or credential changes.
 * present=0 is a successful missing record. Legacy zero dimensions are kept.
 * Save ignores input.present and returns the persisted snapshot. Inputs/outputs
 * must not overlap. Store owns auxiliary bounds and duplicate suppression. */
typedef struct { int present,language,pager,editor; unsigned columns,rows,aux; } MemberPreferences;
MemberOutcome member_core_preferences(const MemberActor *,MemberPreferences *out);
MemberOutcome member_core_save_preferences(const MemberActor *,const MemberPreferences *input,MemberPreferences *out);
#endif
