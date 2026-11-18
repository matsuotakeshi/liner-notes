#ifndef MESSAGE_FLOW_CORE_H
#define MESSAGE_FLOW_CORE_H
#include <stddef.h>
#include <stdint.h>
/* Session-event-loop values. No credentials, persistence, UI or transport. */
typedef struct { int mode,pending;uint32_t cursor;unsigned lines; } GatherFlow;
typedef enum { GATHER_INPUT_TEXT,GATHER_INPUT_NEXT,GATHER_INPUT_FINISH,
               GATHER_INPUT_QUIT,GATHER_INPUT_CANCEL } GatherInput;
typedef enum { GATHER_NO_EFFECT,GATHER_NEXT_PAGE,GATHER_EDIT,GATHER_CLOSED,
               GATHER_ABORTED,GATHER_EMPTY,GATHER_INVALID,GATHER_POST,
               GATHER_SAVED,GATHER_FAILED } GatherEffect;
typedef enum { GATHER_RESPONSE_OTHER,GATHER_RESPONSE_PAGE,GATHER_RESPONSE_SAVED } GatherResponse;
/* Read-only uses legacy mode 2; read-then-write uses mode 1; post waits as 3.
 * Queue rejection must not create a pending completion. */
void gather_flow_read_submitted(GatherFlow *,int read_only,int accepted);
void gather_flow_post_submitted(GatherFlow *,int accepted);
/* line_valid is the existing Activity Core validator result. text and the
 * terminated caller-owned body must not overlap. Body changes only on append;
 * caller clears the complete draft on EDIT/ABORTED/SAVED as before. */
GatherEffect gather_flow_input(GatherFlow *,GatherInput,const char *text,int line_valid,
                              char *body,size_t capacity);
/* Caller MUST check completion session/slot before applying it. */
GatherEffect gather_flow_complete(GatherFlow *,GatherResponse,uint32_t next);

typedef struct { int pending;uint64_t after; } SysopMailFlow;
typedef enum { SYSOP_MAIL_NONE,SYSOP_MAIL_RETRY,SYSOP_MAIL_SENT } SysopMailEffect;
int sysop_mail_ready(const SysopMailFlow *,uint64_t now);
/* Same 1..80 byte/control-character rule as the legacy title adapter. */
int sysop_mail_title_valid(const char *);
void sysop_mail_submitted(SysopMailFlow *,int accepted);
SysopMailEffect sysop_mail_complete(SysopMailFlow *,int sent,uint64_t now);
#endif
