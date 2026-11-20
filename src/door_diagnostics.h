#ifndef LINER_DOOR_DIAGNOSTICS_H
#define LINER_DOOR_DIAGNOSTICS_H
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline int door_diagnostics_enabled(void) {
    const char *value=getenv("LINER_DOOR_DIAGNOSTICS");
    return value && !strcmp(value,"1");
}

/* Opt-in, fixed call-site labels only: never pass request data or paths here. */
static inline void door_launch_diagnostic(const char *phase,int error) {
    int saved=errno;
    if(door_diagnostics_enabled()) {
        fprintf(stderr,"DOOR_LAUNCH_FAILURE phase=%s errno=%d\n",phase,error);
        fflush(stderr);
    }
    errno=saved;
}

/* The wire reader still consumes exactly three bytes. ERR and DON are prefixes,
 * not claims that a complete ERR\n or DONE\n frame was received. */
static inline const char *door_reply_kind(const char *reply,long length) {
    if(length<0)return "IO_ERROR";
    if(!length)return "EOF";
    if(length<3)return "SHORT";
    if(length==3) {
        if(!memcmp(reply,"OK\n",3))return "OK";
        if(!memcmp(reply,"ERR",3))return "ERR_PREFIX";
        if(!memcmp(reply,"DON",3))return "DONE_PREFIX";
    }
    return "OTHER";
}
static inline void door_reply_diagnostic(const char *reply,long length,int descriptors,int truncated,int error) {
    int saved=errno;
    if(door_diagnostics_enabled()) {
        fprintf(stderr,"DOOR_ISOLATION_REPLY result=%s bytes=%ld fds=%d truncated=%d errno=%d\n",
                door_reply_kind(reply,length),length,descriptors,!!truncated,error);
        fflush(stderr);
    }
    errno=saved;
}
#endif
