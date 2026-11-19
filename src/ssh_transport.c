#define _POSIX_C_SOURCE 200809L
#include "ssh_transport.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef LINER_SSH
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
/* The message API allows asynchronous worker authentication. Its deprecated
 * accessors remain supported in libssh 0.10+. Do not suppress other warnings. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
struct LnSshServer { ssh_bind bind;unsigned attempts,timeout;int methods; };
struct LnSsh {
    ssh_session session;ssh_channel channel;ssh_message pending;
    LnSshAuth auth;void *context;uint64_t started;
    unsigned attempts,max_attempts,timeout,columns,rows;
    int methods,kex,authenticated,pty,shell,dead,resized;
};
LnSshServer *ln_ssh_server(const LnSshConfig *c) {
    if(!c || !c->host_key || !c->port || c->port>65535 || !c->attempts || c->attempts>20 || !c->timeout || c->timeout>300)return NULL;
    /* Host private key must not be shared-readable or symlinked. */
    struct stat st;
    if(lstat(c->host_key,&st) || !S_ISREG(st.st_mode) || (st.st_mode&077) ||
       (st.st_uid!=0 && st.st_uid!=geteuid()))return NULL;
    LnSshServer *s=calloc(1,sizeof(*s));if(!s)return NULL;
    s->bind=ssh_bind_new();int config=0;
    if(!s->bind || ssh_bind_options_set(s->bind,SSH_BIND_OPTIONS_PROCESS_CONFIG,&config)!=SSH_OK ||
       ssh_bind_options_set(s->bind,SSH_BIND_OPTIONS_BINDADDR,c->bind)!=SSH_OK ||
       ssh_bind_options_set(s->bind,SSH_BIND_OPTIONS_BINDPORT,&c->port)!=SSH_OK ||
       ssh_bind_options_set(s->bind,SSH_BIND_OPTIONS_HOSTKEY,c->host_key)!=SSH_OK ||
       ssh_bind_listen(s->bind)!=SSH_OK) { ln_ssh_server_free(s);return NULL; }
    ssh_bind_set_blocking(s->bind,0);
    int listener=(int)ssh_bind_get_fd(s->bind),flags=fcntl(listener,F_GETFL);
    if(flags<0 || fcntl(listener,F_SETFL,flags|O_NONBLOCK)<0 || fcntl(listener,F_SETFD,FD_CLOEXEC)<0) { ln_ssh_server_free(s);return NULL; }
    s->attempts=c->attempts;s->timeout=c->timeout;
    s->methods=SSH_AUTH_METHOD_PASSWORD|(c->public_keys?SSH_AUTH_METHOD_PUBLICKEY:0);
    return s;
}
void ln_ssh_server_free(LnSshServer *s) { if(s) { if(s->bind)ssh_bind_free(s->bind);free(s); } }
int ln_ssh_listener(const LnSshServer *s) { return s?(int)ssh_bind_get_fd(s->bind):-1; }
LnSsh *ln_ssh_accept(LnSshServer *server,int fd,uint64_t now,LnSshAuth auth,void *context) {
    LnSsh *s=calloc(1,sizeof(*s));if(!s)return NULL;
    s->session=ssh_new();int copy=dup(fd);
    if(!s->session || copy<0) { if(copy>=0)close(copy);ln_ssh_free(s);return NULL; }
    (void)fcntl(copy,F_SETFD,FD_CLOEXEC);
    if(ssh_bind_accept_fd(server->bind,s->session,copy)!=SSH_OK) { close(copy);ln_ssh_free(s);return NULL; }
    ssh_set_blocking(s->session,0);ssh_set_auth_methods(s->session,server->methods);
    s->methods=server->methods;s->max_attempts=server->attempts;s->timeout=server->timeout;
    s->started=now;s->auth=auth;s->context=context;s->columns=80;s->rows=24;
    return s;
}
void ln_ssh_free(LnSsh *s) {
    if(!s)return;
    if(s->pending)ssh_message_free(s->pending);
    if(s->channel)ssh_channel_free(s->channel);
    if(s->session)ssh_free(s->session);
    free(s);
}
int ln_ssh_ready(const LnSsh *s) { return s && s->authenticated && s->shell && !s->dead; }
int ln_ssh_auth_pending(const LnSsh *s) { return s && s->pending; }
int ln_ssh_auth_result(LnSsh *s,int accepted) {
    if(!s || !s->pending || s->dead)return 0;
    int rc=accepted?ssh_message_auth_reply_success(s->pending,0):ssh_message_reply_default(s->pending);
    ssh_message_free(s->pending);s->pending=NULL;
    if(rc!=SSH_OK) { s->dead=1;return 0; }
    if(accepted)s->authenticated=1;
    else if(s->attempts>=s->max_attempts)s->dead=1;
    return !s->dead;
}
static int key_proof(ssh_message m,char out[65]) {
    if(ssh_message_auth_publickey_state(m)!=SSH_PUBLICKEY_STATE_VALID)return 0;
    unsigned char *hash=NULL;size_t len=0;
    if(ssh_get_publickey_hash(ssh_message_auth_pubkey(m),SSH_PUBLICKEY_HASH_SHA256,&hash,&len)!=SSH_OK)return 0;
    if(len==32)for(size_t i=0;i<len;i++)snprintf(out+i*2,3,"%02x",hash[i]);
    ssh_clean_pubkey_hash(&hash);return len==32;
}
static int dimensions(LnSsh *s,ssh_message m) {
    int w=ssh_message_channel_request_pty_width(m),h=ssh_message_channel_request_pty_height(m);
    if(w<=0 || h<=0 || w>65535 || h>65535)return 0;
    if(!s->pty || s->columns!=(unsigned)w || s->rows!=(unsigned)h)s->resized=1;
    s->columns=(unsigned)w;s->rows=(unsigned)h;return 1;
}
int ln_ssh_pump(LnSsh *s,uint64_t now) {
    if(s->dead || (!s->shell && now-s->started>=(uint64_t)s->timeout*1000))return 0;
    if(!s->kex) {
        int rc=ssh_handle_key_exchange(s->session);
        if(rc==SSH_AGAIN)return 1;
        if(rc!=SSH_OK)return 0;
        s->kex=1;
    }
    /* Bound packet/message work per client per event-loop pass. No password
     * hashing or repository IO takes place on this thread. */
    for(int i=0;i<8 && !s->pending;i++) {
        ssh_message m=ssh_message_get(s->session);if(!m)break;
        int type=ssh_message_type(m),sub=ssh_message_subtype(m),answered=0;
        if(type==SSH_REQUEST_SERVICE && !s->authenticated &&
           !strcmp(ssh_message_service_service(m),"ssh-userauth")) {
            ssh_message_service_reply_success(m);answered=1;
        } else if(type==SSH_REQUEST_AUTH && !s->authenticated) {
            ssh_message_auth_set_methods(m,s->methods);
            const char *user=ssh_message_auth_user(m);char proof[65];const char *credential=NULL;int key=0;
            if(sub!=SSH_AUTH_METHOD_NONE && ++s->attempts>s->max_attempts)s->dead=1;
            if(!s->dead && user && strlen(user)<=32) {
                if(sub==SSH_AUTH_METHOD_PASSWORD)credential=ssh_message_auth_password(m);
                else if(sub==SSH_AUTH_METHOD_PUBLICKEY && (s->methods&SSH_AUTH_METHOD_PUBLICKEY)) {
                    if(ssh_message_auth_publickey_state(m)==SSH_PUBLICKEY_STATE_NONE) {
                        /* A probe grants no identity. Actual signature and Core
                         * member/key binding must both succeed afterwards. */
                        ssh_message_auth_reply_pk_ok_simple(m);answered=1;
                    } else if(key_proof(m,proof)) { credential=proof;key=1; }
                }
                if(credential && strlen(credential)<=128 && s->auth(s->context,user,credential,key)) {
                    s->pending=m;continue;
                }
            }
        } else if(type==SSH_REQUEST_CHANNEL_OPEN && s->authenticated && !s->channel && sub==SSH_CHANNEL_SESSION) {
            s->channel=ssh_message_channel_request_open_reply_accept(m);answered=s->channel!=NULL;
        } else if(type==SSH_REQUEST_CHANNEL && s->authenticated && s->channel &&
                  ssh_message_channel_request_channel(m)==s->channel) {
            if(sub==SSH_CHANNEL_REQUEST_PTY && !s->pty && !s->shell && dimensions(s,m)) { s->pty=1;answered=1; }
            else if(sub==SSH_CHANNEL_REQUEST_WINDOW_CHANGE && s->pty && dimensions(s,m))answered=1;
            else if(sub==SSH_CHANNEL_REQUEST_SHELL && s->pty && !s->shell) { s->shell=1;answered=1; }
            if(answered)ssh_message_channel_request_reply_success(m);
        }
        /* Default deny: exec/SFTP/SCP, env, agent/X11, second channels, direct
         * and remote forwarding, arbitrary services and global requests. */
        if(!answered)ssh_message_reply_default(m);
        ssh_message_free(m);
        if(s->dead)return 0;
    }
    return ssh_is_connected(s->session) && (!s->channel || !ssh_channel_is_closed(s->channel));
}
int ln_ssh_read(LnSsh *s,void *buf,size_t n) {
    if(!ln_ssh_ready(s))return 0;
    int rc=ssh_channel_read_nonblocking(s->channel,buf,(uint32_t)n,0);
    if(rc==SSH_AGAIN)return 0;
    if(rc==SSH_ERROR || (rc==0 && ssh_channel_is_eof(s->channel)))return -1;
    return rc;
}
int ln_ssh_write(LnSsh *s,const void *buf,size_t n) {
    if(!ln_ssh_ready(s))return 0;
    /* Bound the libssh output queue by channel window and transport backpressure. */
    if(ssh_get_status(s->session)&SSH_WRITE_PENDING)return 0;
    uint32_t window=ssh_channel_window_size(s->channel);if(n>window)n=window;if(n>16384)n=16384;
    if(!n)return 0;
    int rc=ssh_channel_write(s->channel,buf,(uint32_t)n);return rc==SSH_AGAIN?0:rc;
}
int ln_ssh_wants_write(const LnSsh *s) { return s && (ssh_get_status(s->session)&SSH_WRITE_PENDING); }
int ln_ssh_dimensions(LnSsh *s,unsigned *w,unsigned *h) { if(!s || !s->resized)return 0;s->resized=0;*w=s->columns;*h=s->rows;return 1; }
#else
struct LnSshServer { int unused; };struct LnSsh { int unused; };
LnSshServer *ln_ssh_server(const LnSshConfig *c) { (void)c;return NULL; }
void ln_ssh_server_free(LnSshServer *s) { (void)s; }
int ln_ssh_listener(const LnSshServer *s) { (void)s;return -1; }
LnSsh *ln_ssh_accept(LnSshServer *s,int fd,uint64_t n,LnSshAuth a,void *c) { (void)s;(void)fd;(void)n;(void)a;(void)c;return NULL; }
void ln_ssh_free(LnSsh *s) { (void)s; }
int ln_ssh_pump(LnSsh *s,uint64_t n) { (void)s;(void)n;return 0; }
int ln_ssh_ready(const LnSsh *s) { (void)s;return 0; }
int ln_ssh_auth_pending(const LnSsh *s) { (void)s;return 0; }
int ln_ssh_auth_result(LnSsh *s,int a) { (void)s;(void)a;return 0; }
int ln_ssh_read(LnSsh *s,void *b,size_t n) { (void)s;(void)b;(void)n;return -1; }
int ln_ssh_write(LnSsh *s,const void *b,size_t n) { (void)s;(void)b;(void)n;return -1; }
int ln_ssh_wants_write(const LnSsh *s) { (void)s;return 0; }
int ln_ssh_dimensions(LnSsh *s,unsigned *w,unsigned *h) { (void)s;(void)w;(void)h;return 0; }
#endif
