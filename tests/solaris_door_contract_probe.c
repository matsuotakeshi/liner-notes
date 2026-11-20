/* Solaris-only foundational probe, NOT authorization to enable isolated Doors.
 * Run as the CI account. The contract contains only our synthetic descendants.
 * NOORPHAN + no INHERIT also cleans up if this probe exits unexpectedly.
 */
#define __EXTENSIONS__ 1
#define _LARGEFILE64_SOURCE 1
#include <sys/types.h>
#include <sys/contract.h>
#include <sys/contract/process.h>
#include <libcontract.h>
#include <priv.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

static void require(int ok, const char *where) {
    if (!ok) { fprintf(stderr, "Solaris Door primitive failed: %s (errno %d)\n", where, errno); exit(1); }
}
static int basic_limit(void) {
    priv_set_t *basic=priv_allocset(), *actual=priv_allocset();
    if (!basic || !actual) return 0;
    priv_basicset(basic);
    int ok=setpflags(PRIV_PFEXEC,0)==0 && setpflags(PRIV_PFEXEC_AUTH,0)==0;
    ok=ok && setppriv(PRIV_SET,PRIV_INHERITABLE,basic)==0;
    ok=ok && setppriv(PRIV_SET,PRIV_EFFECTIVE,basic)==0;
    ok=ok && setppriv(PRIV_SET,PRIV_PERMITTED,basic)==0;
    ok=ok && setppriv(PRIV_SET,PRIV_LIMIT,basic)==0;
    ok=ok && getppriv(PRIV_LIMIT,actual)==0 && priv_isequalset(basic,actual);
    ok=ok && !priv_ismember(actual,PRIV_PROC_SETID) && !priv_ismember(actual,PRIV_PROC_AUDIT);
    priv_freeset(actual); priv_freeset(basic); return ok;
}
int main(int argc, char **argv) {
    int nested=argc==2 && strcmp(argv[1],"--nested")==0;
    require(argc==1 || nested,"usage: probe [--nested]");
    require(getuid()!=0 && getuid()==geteuid(),"non-root CI identity required");
    int channel[2], watchdog[2];
    require(pipe(channel)==0 && pipe(watchdog)==0,"pipes");
    int tmpl=open("/system/contract/process/template",O_RDWR);
    require(tmpl>=0,"template");
    require(ct_tmpl_set_critical(tmpl,0)==0,"critical");
    require(ct_tmpl_set_informative(tmpl,0)==0,"informative");
    require(ct_pr_tmpl_set_fatal(tmpl,0)==0,"fatal");
    require(ct_pr_tmpl_set_param(tmpl,CT_PR_NOORPHAN)==0,"NOORPHAN");
    require(ct_tmpl_activate(tmpl)==0,"activate");
    pid_t child=fork(); require(child>=0,"fork");
    if (child==0) {
        close(channel[0]); close(watchdog[1]);
        if (ct_tmpl_clear(tmpl)!=0) _exit(121);
        close(tmpl);
        if (setsid()<0 || !basic_limit()) _exit(122);
        int escape=-1;
        if (nested) {
            /* An ordinary Door can attempt a second contract without NOORPHAN.
             * Test this after dropping to basic privileges, not as root.
             */
            escape=open("/system/contract/process/template",O_RDWR);
            if (escape<0 || ct_tmpl_set_critical(escape,0)!=0 ||
                ct_tmpl_set_informative(escape,0)!=0 ||
                ct_pr_tmpl_set_fatal(escape,0)!=0 ||
                ct_pr_tmpl_set_param(escape,0)!=0 || ct_tmpl_activate(escape)!=0)
                _exit(127);
        }
        pid_t descendant=fork(); if (descendant<0) _exit(123);
        if (escape>=0) {
            if (ct_tmpl_clear(escape)!=0) _exit(128);
            close(escape);
        }
        if (descendant==0) {
            if (setsid()<0) _exit(124);
            signal(SIGHUP,SIG_IGN);
            if (write(channel[1],"R",1)!=1) _exit(125);
            if (nested) {
                /* This synthetic escape is bounded even if the harness dies:
                 * only the original harness owns the watchdog write end.
                 * Never signal an enumerated/recycled PID to clean up.
                 */
                struct pollfd owner={watchdog[0],POLLIN|POLLHUP,0};
                (void)poll(&owner,1,5000);
                _exit(0);
            }
        }
        for (;;) pause(); /* Both retain the pipe until contract cleanup. */
    }
    close(channel[1]); close(watchdog[0]);
    require(ct_tmpl_clear(tmpl)==0,"clear parent template"); close(tmpl);
    int latest=open("/system/contract/process/latest",O_RDONLY);
    require(latest>=0,"latest owned contract");
    ct_stathdl_t status;
    require(ct_status_read(latest,CTD_COMMON,&status)==0,"contract status");
    ctid_t id=ct_status_get_id(status);
    require(ct_status_get_holder(status)==(id_t)getpid(),"contract ownership");
    ct_status_free(status); close(latest);
    char path[128]; snprintf(path,sizeof(path),"/system/contract/process/%ld/ctl",(long)id);
    int control=open(path,O_WRONLY); require(control>=0,"owned control");
    struct pollfd ready={channel[0],POLLIN,0};
    require(poll(&ready,1,5000)>0,"descendant readiness deadline");
    char marker=0; require(read(channel[0],&marker,1)==1 && marker=='R',"descendant ready");
    require(ct_ctl_abandon(control)==0,"abandon owned NOORPHAN contract"); close(control);
    struct pollfd ended={channel[0],POLLIN|POLLHUP,0};
    if (nested) {
        int observed=poll(&ended,1,250);
        require(observed>=0,"nested observation");
        printf("OBSERVED nested contract survived outer abandonment: %s\n",
               observed==0 ? "yes" : "no");
        /* Cooperatively release the bounded synthetic escape, independently
         * of whether outer-contract cleanup contained it. */
    }
    close(watchdog[1]);
    require(poll(&ended,1,6000)>0,"contract cleanup deadline");
    require(read(channel[0],&marker,1)==0,"both descendants gone"); close(channel[0]);
    int result; require(waitpid(child,&result,0)==child,"reap leader");
    require(WIFSIGNALED(result) && WTERMSIG(result)==SIGKILL,"contract termination");
    if (nested)
        puts("PROBE COMPLETE nested-contract observation; not a containment acceptance");
    else
        puts("PASS Solaris owned-contract cleanup and basic privilege limit primitives");
    puts("ISOLATION NOT VERIFIED: new-contract escape and non-root set-ID execution need separate denial tests");
    return 0;
}
