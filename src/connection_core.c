#include "connection_core.h"
#include <stdio.h>
#include <string.h>

uint64_t connection_elapsed(uint64_t now,uint64_t since) { return now>=since?now-since:0; }
void connection_input(ConnectionClock *clock,uint64_t now) { clock->last_input=now;clock->idle_warned=0; }
ConnectionDecision connection_time_check(const ConnectionPolicy *policy,ConnectionClock *clock,const ConnectionState *state,uint64_t now) {
    if(state->admin || state->closing)return CONNECTION_NONE;
    unsigned level=state->user && state->level<6?state->level:0;
    uint64_t limit=(uint64_t)policy->level_seconds[level]*1000,elapsed=connection_elapsed(now,clock->started);
    if(!limit)return CONNECTION_NONE;
    if(elapsed>=limit+(uint64_t)policy->grace_seconds*1000)return CONNECTION_DROP;
    if(state->door || state->zmodem || state->binary || state->secret)return CONNECTION_NONE;
    if(elapsed>=limit && !clock->time_grace) { clock->time_grace=1;return CONNECTION_GRACE; }
    if(!clock->time_warning && elapsed+(uint64_t)policy->warning_seconds*1000>=limit) {
        clock->time_warning=1;return CONNECTION_WARNING;
    }
    return CONNECTION_NONE;
}
int connection_idle_drained(const ConnectionClock *clock,uint64_t now) { return clock->idle_close && now>=clock->idle_close; }
void connection_busy_activity(ConnectionClock *clock,const ConnectionState *state,uint64_t now) {
    if(state->binary || state->pending || state->zmodem)connection_input(clock,now);
}
ConnectionDecision connection_idle_check(const ConnectionPolicy *policy,ConnectionClock *clock,const ConnectionState *state,uint64_t now) {
    if(!policy->idle_seconds || state->admin || state->closing || state->pending || state->binary || state->zmodem)return CONNECTION_NONE;
    uint64_t elapsed=connection_elapsed(now,clock->last_input),limit=(uint64_t)policy->idle_seconds*1000;
    uint64_t warning=policy->idle_seconds>120?60000:limit/2;
    if(elapsed>=limit) { clock->idle_close=now+1000;return CONNECTION_CLOSE; }
    if(!clock->idle_warned && elapsed>=limit-warning && state->menu && !state->secret) {
        clock->idle_warned=1;return CONNECTION_WARNING;
    }
    return CONNECTION_NONE;
}
ConnectionMode connection_mode(const ConnectionState *state) {
    return state->door?CONNECTION_DOOR:state->zmodem?CONNECTION_ZMODEM:state->binary?CONNECTION_BINARY:
           state->secret?CONNECTION_AUTH:state->chat?CONNECTION_CHAT:state->editing?CONNECTION_EDITOR:CONNECTION_BBS;
}
void connection_shutdown_schedule(ConnectionShutdown *state,uint64_t now,unsigned seconds,const char *reason) {
    state->at=now+(uint64_t)seconds*1000;state->drain=0;snprintf(state->reason,sizeof(state->reason),"%s",reason);
}
void connection_shutdown_cancel(ConnectionShutdown *state) { memset(state,0,sizeof(*state)); }
unsigned connection_shutdown_remaining(const ConnectionShutdown *state,uint64_t now) {
    return now<state->at?(unsigned)((state->at-now+999)/1000):0;
}
ShutdownDecision connection_shutdown_step(ConnectionShutdown *state,uint64_t now) {
    if(state->at && now>=state->at && !state->drain) { state->drain=now+500;return SHUTDOWN_DRAIN; }
    return state->drain && now>=state->drain?SHUTDOWN_STOP:SHUTDOWN_WAIT;
}
