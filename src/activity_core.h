#ifndef ACTIVITY_CORE_H
#define ACTIVITY_CORE_H
#include <stdint.h>
/* Board-worker only. Identity/epoch come from the authenticated session.
 * Each call rechecks authority. Inputs must not alias owned output snapshots;
 * all output fields are cleared on failure. No UI, sockets or runtime handles. */
typedef struct { uint32_t user,epoch; } ActivityActor;
typedef enum {
    ACTIVITY_OK,ACTIVITY_LOGIN,ACTIVITY_REVOKED,ACTIVITY_DENIED,
    ACTIVITY_MEMBER_STORAGE,ACTIVITY_MODERATION_STORAGE,ACTIVITY_PROFILE_STORAGE,
    ACTIVITY_STORAGE,ACTIVITY_DISABLED,ACTIVITY_INVALID,ACTIVITY_NO_MEMBER
} ActivityOutcome;
#define ACTIVITY_BODY 3000
/* One bounded retained message; end marks the valid cursor just after the last
 * row. Cursor is a retained-list offset, not a durable sequence/unique ID. */
typedef struct {
    uint32_t actor,sequence;uint64_t time;
    unsigned next;int end;
    char name[33],handle[81],body[ACTIVITY_BODY+1];
} ActivityGather;
int gather_line_valid(const char *);
int activity_gather_valid(const char *);
ActivityOutcome activity_gather_read(const ActivityActor *,unsigned cursor,ActivityGather *);
ActivityOutcome activity_gather_post(const ActivityActor *,const char *body);
typedef enum { ACTIVITY_HISTORY,ACTIVITY_STATS,ACTIVITY_TABLE } ActivityLogKind;
typedef struct { ActivityLogKind kind;uint32_t target;unsigned cursor;int all; } ActivityLogRequest;
ActivityOutcome activity_userlog(const ActivityActor *,ActivityLogKind,uint32_t target,
                                unsigned cursor,int all,ActivityLogRequest *);
typedef enum {
    ACTIVITY_CONNECTIONS,ACTIVITY_ANNOUNCE,ACTIVITY_DISCONNECT,
    ACTIVITY_SHUTDOWN,ACTIVITY_CANCEL_SHUTDOWN
} ActivityControlKind;
typedef struct { ActivityControlKind kind;uint32_t number;char text[513]; } ActivityControl;
/* Audit commits before returning a runtime effect. Connections is read-only.
 * A committed audit records an accepted request, NOT completion of a runtime
 * effect (e.g. disconnect may find no connection). Consume once, immediately. */
ActivityOutcome activity_control(const ActivityActor *,ActivityControlKind,uint32_t,
                                 const char *text,ActivityControl *);
#endif
