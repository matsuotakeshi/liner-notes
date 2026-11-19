#ifndef MEMBER_CORE_H
#define MEMBER_CORE_H
#include <stdint.h>
#include <stddef.h>
#include "chat_preferences.h"
#include "profile_store.h"
#include "application_repository.h"

/* Internal, worker-owned account service. Trusted session supplies its
 * authenticated ID/epoch; no caller admin flag or cached authorization. No
 * socket/UI dependency. Outputs own their data and clear on failure.
 * Input/output storage must not overlap; no pointer is retained. */
typedef struct { uint32_t user,epoch; } MemberActor;
typedef enum { MEMBER_CORE_OK, MEMBER_CORE_REVOKED, MEMBER_CORE_STORAGE,
               MEMBER_CORE_MODERATION_STORAGE, MEMBER_CORE_ADMIN_REQUIRED,
               MEMBER_CORE_NOT_FOUND, MEMBER_CORE_INVALID, MEMBER_CORE_PROTECTED,
               MEMBER_CORE_EXTRAS_STORAGE, MEMBER_CORE_MODERATION_UNCERTAIN,
               MEMBER_CORE_LOGIN_REQUIRED, MEMBER_CORE_RIGHTS_INVALID, MEMBER_CORE_STATE, MEMBER_CORE_PASSWORD, MEMBER_CORE_PREFERENCES,
               MEMBER_CORE_PROFILE_TEXT, MEMBER_CORE_PROFILE_ADMINISTRATOR,
               MEMBER_CORE_PROFILE_UNAVAILABLE, MEMBER_CORE_PROFILE_SEARCH_DENIED, MEMBER_CORE_PROFILE_EDIT,
               MEMBER_CORE_APPLICATION_INVALID, MEMBER_CORE_SETTINGS_STORAGE } MemberOutcome;
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
/* Global-admin bulk group migration, groups 1..100 and distinct. Store commits
 * one record even with no source memberships. Then all named account epochs
 * advance, including pending/suspended/retired/protected accounts. Caller
 * publishes vault notifications through the inclusive bound and revokes other
 * connections; actor_epoch keeps this administrator's current connection alive.
 * Worker serializes the commit and publication; this result is not reusable. */
typedef struct { uint32_t through,actor_epoch; } MemberGroupMove;
MemberOutcome member_core_group_move(const MemberActor *,unsigned from,unsigned to,MemberGroupMove *out);
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
/* Empty preference means the station default; only the authenticated owner. */
MemberOutcome member_core_ui_get(const MemberActor *,char out[32]);
MemberOutcome member_core_ui_save(const MemberActor *,const char *name);
/* Own-account mail arrival setting. Query is read-only; ON/OFF retain store
 * duplicate suppression and revision-overflow handling. Only success returns
 * a user/revision snapshot for the caller to publish to live sessions. Core
 * retains no connection and never changes credential epochs. */
typedef enum { MEMBER_NOTIFY_QUERY, MEMBER_NOTIFY_ON, MEMBER_NOTIFY_OFF } MemberNotifyAction;
typedef struct { uint32_t user,revision; int deny; } MemberMailNotify;
MemberOutcome member_core_mail_notify(const MemberActor *,MemberNotifyAction,MemberMailNotify *out);
/* Admin-only chat removal order, committed to moderation audit before output.
 * login is borrowed (1..32 bytes, exact case); no pointer is retained. Existing
 * named targets remain eligible regardless of connection/account status.
 * Self/protected targets are denied. Caller applies the successful order only
 * to chat connections; this does not suspend accounts or change epochs. */
typedef struct { uint32_t user; char name[33]; } MemberChatKick;
MemberOutcome member_core_chat_kick(const MemberActor *,const char *login,MemberChatKick *out);
/* Own chat preferences. QUERY/receive actions require NULL login; mute actions
 * require an exact 1..32-byte login. Output owns the complete 32-slot snapshot.
 * Failures clear output; settings revisions never alter credential epochs. */
MemberOutcome member_core_chat_preferences(const MemberActor *,ChatPreferenceAction,const char *login,ChatPreferences *out);
/* Own profile mutations: no target or caller-provided policy/role flag. Current
 * actor and live compatibility policy are checked for each write. Text is a
 * borrowed NUL-terminated string; field validation/UTF-8, durable journal and
 * audit attribution remain in profile_store. No private text is returned.
 * Profile fields and the shared buffer use profile_store.h field identifiers.
 * Visibility excludes the buffer; mode 0=private, 1=members, 2=birthday month/day.
 * Only successful visibility updates require the adapter's board refresh. */
MemberOutcome member_core_profile_set(const MemberActor *,unsigned field,const char *text);
MemberOutcome member_core_profile_visibility(const MemberActor *,unsigned field,unsigned mode);
/* Visible, owned snapshot: at most 12 KiB, never all private store fields.
 * target=0/login=NULL selects self; otherwise exactly one ID or exact login.
 * The caller must authorize an article before resolving its author here.
 * Value pointers belong to out, never the store's rotating scratch cache.
 * All outputs clear on failure; input/output memory must not overlap. */
typedef struct {
    uint32_t user; char name[33],handle[81],bio[8193];
    char fields[PROFILE_FIELDS-2][321];
} MemberProfile;
MemberOutcome member_core_profile_get(const MemberActor *,uint32_t target,const char *login,MemberProfile *out);
const char *member_profile_value(const MemberProfile *,unsigned field);
/* Typed search (field is zero based). Inclusive start; zero starts at 1.
 * limit=1..8; next is the next unscanned ID, preserving legacy paging.
 * Policy/ACL is rechecked per page. No total count or hidden matching values. */
typedef enum { MEMBER_PROFILE_ALL, MEMBER_PROFILE_FIELD, MEMBER_PROFILE_GROUP } MemberProfileScope;
typedef struct {
    MemberProfileScope scope; unsigned field,group; uint32_t start;
    unsigned limit; const char *text;
} MemberProfileQuery;
typedef struct { uint32_t user; char name[33],handle[81]; } MemberProfileRow;
typedef struct { uint32_t next; unsigned count; MemberProfileRow rows[8]; } MemberProfilePage;
MemberOutcome member_core_profile_search(const MemberActor *,const MemberProfileQuery *,MemberProfilePage *out);
/* Global-admin editing, including own/protected/pending/suspended active accounts.
 * No shared-buffer edit or epoch change. Store retains validation and actor audit. */
MemberOutcome member_core_profile_admin_set(const MemberActor *,uint32_t target,unsigned field,const char *text);
/* Full pending-application view is a separate administrator-only operation.
 * It includes private profile fields and optional FROMBBS/SYSOPNOTE; never use
 * this result as an ordinary member profile. Approval/removal invalidates review
 * eligibility immediately. Results own their memory and clear on every failure.
 * Notes edits preserve profile visibility, membership state and auth epochs. */
typedef struct { MemberProfile profile; ApplicationNotes notes; } MemberApplication;
MemberOutcome member_core_application_get(const MemberActor *,uint32_t target,MemberApplication *out);
MemberOutcome member_core_application_set(const MemberActor *,uint32_t target,ApplicationNoteField,const char *value);
/* Own private editing documents only; no target-member/admin override. Kind is
 * independent of profile field numbers. Reads copy to caller-owned storage and
 * clear it on all errors. Save/clear do not alter draft revisions or visibility.
 * Buffers must be distinct from input/store memory. Calls remain worker-serialized. */
typedef enum { MEMBER_DOCUMENT_BIO, MEMBER_DOCUMENT_BUFFER } MemberDocument;
MemberOutcome member_core_document_get(const MemberActor *,MemberDocument,char *body,size_t capacity);
MemberOutcome member_core_document_save(const MemberActor *,MemberDocument,const char *body);
/* Persisted guidance policy, not a permission or guest preference. */
typedef enum { MEMBER_MENU_BEGINNER, MEMBER_MENU_STANDARD, MEMBER_MENU_EXPERT } MemberMenuMode;
MemberOutcome member_core_menu_get(const MemberActor *,MemberMenuMode *out);
MemberOutcome member_core_menu_save(const MemberActor *,MemberMenuMode);
/* Actor-owned checkpoint keys retain legacy storage: empty = all; article
 * scope = canonical board:0 (here) or board:2 (subtree); files = path <=48 bytes;
 * mail only accepts empty. Keys select cursors, NEVER authorize content access.
 * Begin is read-only; caller commits the captured until after traversal ends.
 * Commit is monotonic, rejects future/out-of-SQLite-range stamps and rechecks
 * credentials. Concurrent older completions cannot move a newer cursor back. */
typedef enum { MEMBER_CHECK_ARTICLES, MEMBER_CHECK_MAIL, MEMBER_CHECK_FILES } MemberCheckKind;
typedef struct { uint64_t since,until; } MemberCheckWindow;
MemberOutcome member_core_check_begin(const MemberActor *,MemberCheckKind,const char *scope,MemberCheckWindow *out);
MemberOutcome member_core_check_commit(const MemberActor *,MemberCheckKind,const char *scope,uint64_t until);
#endif
