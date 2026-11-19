#ifndef AUTHENTICATION_CORE_H
#define AUTHENTICATION_CORE_H
#include <stddef.h>
#include <stdint.h>
#include "chat_preferences.h"
#include "profile_store.h"
/* Synchronous, serialized by the account/board worker. Credentials are borrowed
 * for this call, never retained or returned. Callers own input cleansing and
 * connection rate limits. This API grants no OS shell or transport access. */
typedef enum { AUTH_LOGIN,AUTH_REGISTER,AUTH_SSH_KEY } AuthenticationMode;
enum { AUTH_FROM_BBS=PROFILE_FIELDS,AUTH_SYSOP_NOTE,AUTH_FORM_FIELDS=2048,AUTH_FORM_BYTES=8192 };
typedef struct { unsigned field;const char *value; } AuthenticationField;
/* Ordered entries preserve duplicate fields and partial-save failure behavior.
 * valid=0 is a decoder failure, evaluated AFTER health/registration policy. */
typedef struct { int valid;size_t count;const AuthenticationField *fields; } AuthenticationForm;
typedef struct { AuthenticationMode mode;const char *login,*password;const AuthenticationForm *form; } AuthenticationRequest;
typedef enum { AUTHENTICATED,AUTH_REJECTED,AUTH_APPLICATION_PENDING,
    AUTH_REGISTRATION_CLOSED,AUTH_APPLICATION_INVALID,AUTH_APPLICATION_STORAGE,
    AUTH_BOARD_STORAGE,AUTH_EXTRAS_STORAGE,AUTH_MEMBER_STORAGE,AUTH_MODERATION_STORAGE } AuthenticationOutcome;
typedef struct {
    uint32_t user,epoch,mail_unread,mail_draft,saved_draft,unread_articles,unread_boards;
    int mail_deny;uint32_t mail_revision;
    int admin,preferences,language,pager,menu_mode,birthday;
    ChatPreferences chat;
    char login[33],handle[81];
} AuthenticationSession;
/* Only AUTHENTICATED returns a session. Pending/reserved-account results can
 * return account_name and automatic_id, never an authenticated ID or epoch.
 * Empty outputs do not mean a failed registration rolled back prior writes.
 * Inputs must not alias out; no pointers into repositories escape. */
typedef struct { AuthenticationSession session;char account_name[33];int automatic_id; } AuthenticationResult;
AuthenticationOutcome authentication_core(const AuthenticationRequest *,AuthenticationResult *out);
#endif
