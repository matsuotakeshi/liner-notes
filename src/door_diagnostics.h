#ifndef LINER_DOOR_DIAGNOSTICS_H
#define LINER_DOOR_DIAGNOSTICS_H
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Opt-in, fixed call-site labels only: never pass request data or paths here. */
static inline void door_launch_diagnostic(const char *phase,int error) {
    int saved=errno;
    const char *enabled=getenv("LINER_DOOR_DIAGNOSTICS");
    if(enabled && !strcmp(enabled,"1")) {
        fprintf(stderr,"DOOR_LAUNCH_FAILURE phase=%s errno=%d\n",phase,error);
        fflush(stderr);
    }
    errno=saved;
}
#endif
