#ifndef LINER_CONNECTION_CORE_H
#define LINER_CONNECTION_CORE_H
#include <stdint.h>
#include <stddef.h>
/* Event-loop owned. Clock values are monotonic milliseconds. This module makes
 * decisions only; adapters retain authentication, IO, cleanup and translation. */
typedef struct {
    uint32_t idle_seconds,level_seconds[6],warning_seconds,grace_seconds;
} ConnectionPolicy;
typedef struct {
    uint64_t started,last_input,idle_close;
    int idle_warned,time_warning,time_grace;
} ConnectionClock;
typedef struct {
    uint32_t user;unsigned level;
    int admin,closing,door,zmodem,binary,pending,secret,menu,chat,editing;
} ConnectionState;
typedef enum { CONNECTION_NONE,CONNECTION_WARNING,CONNECTION_GRACE,
               CONNECTION_CLOSE,CONNECTION_DROP } ConnectionDecision;
void connection_input(ConnectionClock *,uint64_t);
ConnectionDecision connection_time_check(const ConnectionPolicy *,ConnectionClock *,const ConnectionState *,uint64_t);
int connection_idle_drained(const ConnectionClock *,uint64_t);
void connection_busy_activity(ConnectionClock *,const ConnectionState *,uint64_t);
ConnectionDecision connection_idle_check(const ConnectionPolicy *,ConnectionClock *,const ConnectionState *,uint64_t);
uint64_t connection_elapsed(uint64_t,uint64_t);

typedef enum { CONNECTION_BBS,CONNECTION_EDITOR,CONNECTION_CHAT,CONNECTION_AUTH,
               CONNECTION_BINARY,CONNECTION_ZMODEM,CONNECTION_DOOR } ConnectionMode;
ConnectionMode connection_mode(const ConnectionState *);
/* Trusted administrative snapshot: contains no secrets or borrowed pointers. */
typedef struct {
    unsigned long id;ConnectionMode mode;int telnet; /* 0 RAW, 1 TELNET, 2 SSH */
    char login[33],peer[64];uint64_t elapsed,idle,rx,tx;
    size_t queued;unsigned columns,rows;
} ConnectionSummary;

typedef struct { uint64_t at,drain;char reason[513]; } ConnectionShutdown;
typedef enum { SHUTDOWN_WAIT,SHUTDOWN_DRAIN,SHUTDOWN_STOP } ShutdownDecision;
/* Invoked only for an already authorized/audited ActivityControl effect. */
void connection_shutdown_schedule(ConnectionShutdown *,uint64_t,unsigned,const char *);
void connection_shutdown_cancel(ConnectionShutdown *);
unsigned connection_shutdown_remaining(const ConnectionShutdown *,uint64_t);
ShutdownDecision connection_shutdown_step(ConnectionShutdown *,uint64_t);
#endif
