#ifndef SSH_TRANSPORT_H
#define SSH_TRANSPORT_H
#include <stddef.h>
#include <stdint.h>
typedef struct LnSshServer LnSshServer;
typedef struct LnSsh LnSsh;
typedef struct { const char *bind,*host_key;unsigned port,attempts,timeout;int public_keys; } LnSshConfig;
/* Event-loop only. submit copies credentials into the existing serialized Core
 * worker. key=1 means libssh has verified a signature, proof is SHA256 hex.
 * Return acceptance, then call ln_ssh_auth_result for this connection only. */
typedef int (*LnSshAuth)(void *,const char *user,const char *proof,int key);
LnSshServer *ln_ssh_server(const LnSshConfig *);
void ln_ssh_server_free(LnSshServer *);
int ln_ssh_listener(const LnSshServer *);
LnSsh *ln_ssh_accept(LnSshServer *,int fd,uint64_t now,LnSshAuth,void *);
void ln_ssh_free(LnSsh *);
int ln_ssh_pump(LnSsh *,uint64_t now);
int ln_ssh_ready(const LnSsh *);
int ln_ssh_auth_pending(const LnSsh *);
int ln_ssh_auth_result(LnSsh *,int accepted);
int ln_ssh_read(LnSsh *,void *,size_t);
int ln_ssh_write(LnSsh *,const void *,size_t);
int ln_ssh_wants_write(const LnSsh *);
int ln_ssh_dimensions(LnSsh *,unsigned *,unsigned *);
#endif
