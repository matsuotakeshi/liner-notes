#ifndef SESSION_UI_H
#define SESSION_UI_H
#include "session_core.h"
#include "messages.h"
#include "chat_types.h"
#include <stddef.h>

typedef struct Session Session;
typedef struct {
    Language language;
    int menu,secret,editing,chatting,chat_pending,automatic,redraw;
} SessionUiSnapshot;
typedef enum { SESSION_NOTICE_DOOR,SESSION_NOTICE_PROGRESS,SESSION_NOTICE_TIME_WARNING,
    SESSION_NOTICE_TIME_GRACE,SESSION_NOTICE_IDLE_WARNING,SESSION_NOTICE_IDLE_CLOSED } SessionNotice;
/* Compiled, trusted adapters only: this is not a dynamically loaded plugin ABI.
 * Callbacks run on the Session event loop. Inputs/views are borrowed for the call.
 * Core receives worker effects BEFORE result; callbacks never authenticate users.
 * legacy is authorized worker output for compatibility renderers, not a wire API.
 */
typedef struct {
    const char *name;
    size_t state_size; /* zero-initialized private bytes; cleared before free */
    int login_required;
    int (*keys)(const Session *);
    void (*key)(Session *,unsigned char);
    SessionUiSnapshot (*snapshot)(const Session *);
    /* reason: 0 disconnect, 1 revoke, 2 discard inactive state; notify only on revoke. */
    void (*cleanup)(Session *,int reason,int notify);
    int (*connector_line)(Session *,char *,size_t);
    int (*binary_byte)(Session *,unsigned char,uint64_t);
    void (*prepare_request)(Session *,OperationRequest *);
    int (*preferences)(const Session *,OperationRequest *);
    uint64_t (*signature)(const Session *);
    void (*scan_pause)(Session *);
    void (*scan_stop)(Session *);
    void (*board_refresh)(Session *);
    void (*access_refresh)(Session *);
    void (*chat_kick)(Session *);
    int (*chat_event)(Session *,const ChatEvent *);
    void (*announce)(Session *,const char *);
    void (*notify)(Session *,MessageId,uint32_t,const char *);
    void (*mail_arrived)(Session *,uint32_t);
    void (*mail_notify)(Session *,uint32_t,int,uint32_t);
    void (*transfer_receipt)(Session *,const char *);
    int (*personal_receive)(Session *,const char *,size_t);
    void (*transfer_finish)(Session *,int,unsigned,uint64_t,int);
    int (*notice)(const Session *,SessionNotice,unsigned,uint64_t,uint64_t,char *,size_t);
    void (*enter)(Session *,int initial,int menu,Language);
    void (*leave)(Session *);
    int (*can_leave)(const Session *);
    void (*line)(Session *,char *,size_t);
    int (*keep_position)(Session *,const OperationResult *);
    void (*result)(Session *,const OperationResult *legacy,SessionResultView,uint64_t,int cancelled);
    void (*emit)(Session *,const char *);
    void (*prompt)(Session *);
    void (*menu)(Session *,int);
    void (*help)(Session *,const char *);
    int (*uppercase)(const Session *,const char *,size_t);
    void (*tick)(Session *,uint64_t);
    void (*door_finish)(Session *);
} SessionUiAdapter;

enum { SESSION_UI_MAX=8 };
typedef enum { SESSION_UI_FIXED,SESSION_UI_MEMBERS,SESSION_UI_ALL } SessionUiPolicy;
typedef struct {
    const SessionUiAdapter *adapters[SESSION_UI_MAX];
    unsigned count,default_index;
    unsigned allowed; /* bits refer to adapters[]; default must be allowed */
    SessionUiPolicy policy;
} SessionUiConfig;
typedef struct {
    SessionUiConfig config; /* per-connection policy snapshot */
    char preferred[32];int explicit_choice,restore_pending;
    const SessionUiAdapter *active;
    void *state;
    void *states[SESSION_UI_MAX];
} SessionUiBinding;
typedef enum { SESSION_UI_SELECTED,SESSION_UI_UNCHANGED,SESSION_UI_UNKNOWN,
    SESSION_UI_DENIED,SESSION_UI_BUSY,SESSION_UI_INVALID } SessionUiSelection;

const SessionUiAdapter *session_ui_rtbbs(void);
const SessionUiAdapter *session_ui_mmm(void);
const SessionUiAdapter *session_ui_mash(void);
const SessionUiAdapter *session_ui_ktbbs(void);
const SessionUiAdapter *session_ui_big_model(void);
const SessionUiAdapter *session_ui_wani_bbs(void);
const SessionUiAdapter *session_ui_net_cock(void);
SessionUiConfig session_ui_default(void);
int session_ui_configure(SessionUiConfig *,const char *,const char *,const char *);
SessionUiSelection session_ui_preflight(const Session *,const char *);
SessionUiSelection session_ui_choose(Session *,const char *);
int session_ui_legacy_connector(Session *);
int session_ui_save_choice(Session *,const char *);
/* Config is copied; immutable adapters and their names must outlive the Session.
 * Registration/configuration is administrator-side C API, never client input.
 */
int session_ui_config_valid(const SessionUiConfig *);
SessionUiSelection session_ui_select(Session *,const char *name);
const char *session_ui_name(const Session *);
SessionUiSnapshot session_ui_snapshot(const Session *);
/* Session lifecycle only; no UI command should call these ownership helpers. */
int session_ui_allocate(Session *,unsigned index);
void session_ui_discard_inactive(Session *);
void session_ui_dispose(Session *);
#endif
