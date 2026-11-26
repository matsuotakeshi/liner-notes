#ifndef SESSION_CORE_H
#define SESSION_CORE_H
#include "operation.h"
#include <stdint.h>
typedef struct {
    unsigned long id;
    int slot;
    int active;
    int structured; /* opt-in structured content in worker completions */
    int login_required;
    int board_enabled;
    int member_mode;
    uint64_t connected_at;
    char guest_handle[33];
    uint64_t now;
    uint64_t wake;
    uint64_t auth_after;
    int closing;
    int pending;
    uint32_t board_grants[1025];
    uint32_t vault_grants[1025];
    uint32_t current;
    uint32_t user;
    uint32_t board;
    uint32_t user_epoch,mail_notify_revision;
    int mail_notify_deny;
    unsigned member_level;
    int admin,sigop;
    char username[33];
} SessionCore;
/* Single event-loop owner. Queue copies the request synchronously; no pointer is
 * retained. Domain workers still validate epoch, account state and ACL. */
typedef struct { void *context; int (*enqueue)(void *,const OperationRequest *); } SessionOperationPort;
typedef enum { SESSION_OPERATION_ACCEPTED,SESSION_OPERATION_INACTIVE,
    SESSION_OPERATION_BUSY,SESSION_OPERATION_THROTTLED,SESSION_OPERATION_UNAVAILABLE,
    SESSION_OPERATION_LOGIN_REQUIRED } SessionOperationStatus;
SessionOperationStatus session_core_submit(SessionCore *,OperationRequest *,SessionOperationPort);
/* Event-loop owner only. Init starts a fresh identity, not an authenticated user. */
void session_core_init(SessionCore *,unsigned long id,int slot,int boards,int members);
int session_core_matches(const SessionCore *,const OperationResult *);
/* Complete atomically validates routing and applies identity, grants, password
 * epoch and position. keep_position is for cancelling a queued read; it cannot
 * suppress revocation or identity changes. No presentation text is inspected. */
int session_core_complete(SessionCore *,const OperationResult *,uint64_t now);
int session_core_complete_mode(SessionCore *,const OperationResult *,uint64_t now,int keep_position);
/* No logout while a worker request is outstanding. Revoke always invalidates. */
int session_core_logout(SessionCore *);
void session_core_revoke(SessionCore *);
void session_core_abort(SessionCore *);
void session_core_close(SessionCore *);
void session_core_throttle(SessionCore *,uint64_t now);
int session_core_password_epoch(SessionCore *,unsigned long session,uint32_t user,uint32_t epoch);
void session_core_clear_vault_grants(SessionCore *);
/* Borrowed typed projection; unavailable/failed payloads are never exposed.
 * The trusted worker owns the result; the view expires when it is reused.
 * Rendering legacy text and UI navigation prompts belong to adapters. */
typedef enum { SESSION_RESULT_OK,SESSION_RESULT_FAILED,SESSION_RESULT_REVOKED,SESSION_RESULT_IGNORED } SessionResultStatus;
typedef struct {
    SessionResultStatus status;
    const OperationFeedback *effects;
    const OperationContent *content;
    const OperationPage *page;
    uint32_t sequence_next;
    const char *body;
    const MailAttachments *attachments;
} SessionResultView;
SessionResultView session_core_result(const OperationResult *);
/* Preferred headless receive: no payload is returned for a stale completion. */
SessionResultView session_core_receive(SessionCore *,const OperationResult *,uint64_t now,int keep_position);
#endif
