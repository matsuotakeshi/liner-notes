#ifndef SESSION_UI_H
#define SESSION_UI_H
#include "session_core.h"
#include "messages.h"
#include <stddef.h>

typedef struct Session Session;
/* Compiled, trusted adapters only: this is not a dynamically loaded plugin ABI.
 * Callbacks run on the Session event loop. Inputs/views are borrowed for the call.
 * Core receives worker effects BEFORE result; callbacks never authenticate users.
 * legacy is authorized worker output for compatibility renderers, not a wire API.
 */
typedef struct {
    const char *name;
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
    const SessionUiAdapter *active;
} SessionUiBinding;
typedef enum { SESSION_UI_SELECTED,SESSION_UI_UNCHANGED,SESSION_UI_UNKNOWN,
    SESSION_UI_DENIED,SESSION_UI_BUSY,SESSION_UI_INVALID } SessionUiSelection;

const SessionUiAdapter *session_ui_rtbbs(void);
SessionUiConfig session_ui_default(void);
/* Config is copied; immutable adapters and their names must outlive the Session.
 * Registration/configuration is administrator-side C API, never client input.
 */
int session_ui_config_valid(const SessionUiConfig *);
SessionUiSelection session_ui_select(Session *,const char *name);
const char *session_ui_name(const Session *);
#endif
