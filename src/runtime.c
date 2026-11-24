/* Liner notes: a native C BBS inspired by RTBBS. No legacy ASM/ABI emulation. */
#define _POSIX_C_SOURCE 200809L
#include "runtime_limits.h"
#include "connection_core.h"
#include "connection_protocol.h"
#include "storage_sqlite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include "board.h"
#include "app_limits.h"
#include "activity.h"
#include "userlog.h"
#include "vault.h"
#include "ui.h"
#include "session.h"
#include "ssh_transport.h"
#include "doors.h"
#include "door_diagnostics.h"
#include "door_lock.h"
#include "store_limits.h"
#include "chat.h"
#include "menu.h"
#include "vault_bridge.h"
#include "vault_worker.h"
#include "terminal.h"
#include "zmodem_host.h"
#include "members.h"
#include "transfer_line.h"
#include <limits.h>
#include <openssl/crypto.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET socket_t;
typedef WSAPOLLFD pollfd_t;
#define INVALID INVALID_SOCKET
#define close_socket closesocket
#define poll_sockets WSAPoll
static int retryable(void) { int e=WSAGetLastError(); return e==WSAEWOULDBLOCK || e==WSAEINTR; }
static int nonblock(socket_t s) { u_long one=1; return ioctlsocket(s,FIONBIO,&one); }
static uint64_t now_ms(void) { return GetTickCount64(); }
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
typedef int socket_t;
typedef struct pollfd pollfd_t;
#define INVALID (-1)
#define close_socket close
#define poll_sockets poll
static int retryable(void) { return errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR; }
static int nonblock(socket_t s) { int f=fcntl(s,F_GETFL); return f<0 ? -1 : fcntl(s,F_SETFL,f|O_NONBLOCK); }
static uint64_t now_ms(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)) { perror("clock_gettime"); exit(1); }
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
#endif

static int listener_error(socket_t listener, const char *operation,
                          const char *address, unsigned port) {
#ifdef _WIN32
    int error=WSAGetLastError();
    fprintf(stderr,"listen setup failed: %s %s:%u: Winsock error %d\n",
            operation,address,port,error);
#else
    int error=errno;
    fprintf(stderr,"listen setup failed: %s %s:%u: %s (errno=%d)\n",
            operation,address,port,strerror(error),error);
#endif
    if(listener!=INVALID)close_socket(listener);
    return 1;
}

#define SLOTS CONNECTION_MAX
#define CAP (INPUT_LINE_MAX+4096)
#define OUT_CAP (CONTENT_DISPLAY*2)
#include <stddef.h>
typedef struct {
    socket_t fd;
    char peer[64];uint64_t rx_bytes,tx_bytes;
    int userlog_registered;ConnectionClock clock;
    LnSsh *ssh;
    Session app;
    Terminal terminal;
    unsigned char input[CAP],output[OUT_CAP];
    size_t used,queued,output_highwater;
    Door *door;ZHost *zhost;int zgeneration,zresult,zrecovery;unsigned zid;uint64_t zsize,zend,zquiet,zstarted;
} Connection;
static Connection connections[SLOTS];
static unsigned long next_id=1;
static const char *zhelper;static unsigned service_port;static int next_zgeneration=1;
static uint32_t account_epochs[MEMBER_MAX+1];
static int board_enabled,member_mode,menu_mode;
static TerminalMode terminal_mode=TERMINAL_RAW;
static const char *admins_path;
static const char *link_accounts_path;
static Language default_language=LANGUAGE_JA;
static ConnectionShutdown shutdown_state;
static volatile sig_atomic_t stopping;
static int log_failed;
static ConnectionPolicy policy={.idle_seconds=600,.warning_seconds=60,.grace_seconds=30};
static void stop(int sig) { (void)sig; stopping=1; }
static void drop(Connection *c) {
    if(c->fd==INVALID)return; /* Unused slots contain no live secrets/resources. */
    if(session_disconnect_preferences(&c->app,NULL)) {
        BoardRequest *q=calloc(1,sizeof(*q));
        if(q) { session_disconnect_preferences(&c->app,q);
            if(!board_save_disconnect(q)) { fprintf(stderr,"disconnect preferences: queue failed\n");log_failed=1; }free(q);
        } else { fprintf(stderr,"disconnect preferences: allocation failed\n");log_failed=1; }
    }
    if(c->fd!=INVALID && c->userlog_registered && (!userlog_update(c->app.core.slot,c->app.core.user,now_ms()) || !userlog_disconnect(c->app.core.slot,now_ms()))) { fprintf(stderr,"userlog: disconnect write failed\n");log_failed=1;stopping=1; }
    door_stop(c->door);c->door=NULL;
    zhost_stop(c->zhost);c->zhost=NULL;
    vault_worker_session(c->app.core.slot,c->app.core.id,0);
    ln_ssh_free(c->ssh);c->ssh=NULL;
    if(c->fd!=INVALID) close_socket(c->fd);
    session_dispose(&c->app); terminal_dispose(&c->terminal);
    /* Only output bytes ever written can contain data. Avoid touching several
     * MiB of unused BSS for each idle connection while shutting down. */
    OPENSSL_cleanse(c->input,sizeof(c->input));OPENSSL_cleanse(c->output,c->output_highwater);
    OPENSSL_cleanse(c,offsetof(Connection,app));
    OPENSSL_cleanse((char *)c+offsetof(Connection,used),sizeof(*c)-offsetof(Connection,used));c->fd=INVALID;
}
static ConnectionState connection_state(Connection *c) {
    Session *s=&c->app;SessionUiSnapshot ui=session_ui_snapshot(s);
    return (ConnectionState){s->core.user,s->core.member_level,s->core.admin,s->core.closing,s->connection.door,s->connection.zmodem,
        s->connection.binary,s->core.pending,ui.secret || (c->ssh && !ln_ssh_ready(c->ssh)),ui.menu,ui.chatting,ui.editing};
}
static int app_drained(void *context) { return ((Connection *)context)->queued==0; }
static int queue_bytes(void *context,const unsigned char *data,size_t size) {
    Connection *c=context;
    if(size>OUT_CAP-c->queued) return 0;
    memcpy(c->output+c->queued,data,size); c->queued+=size;if(c->queued>c->output_highwater)c->output_highwater=c->queued;return 1;
}
static int app_write(void *context,const char *data,size_t size) {
    Connection *c=context;
    terminal_set_binary(&c->terminal,c->app.connection.binary || c->app.connection.zmodem || c->app.connection.door);
    if(!terminal_write(&c->terminal,(const unsigned char *)data,size))return 0;
    /* Reprint a partially typed chat line after an asynchronous prompt. The
     * editor buffer is unchanged; never echo a password or binary payload. */
    if(session_ui_snapshot(&c->app).redraw && c->terminal.size && c->terminal.mode!=TERMINAL_RAW)
        return terminal_write(&c->terminal,(unsigned char *)c->terminal.line,c->terminal.size);
    return 1;
}
static int ssh_authenticate(void *context,const char *name,const char *proof,int key) {
    Connection *c=context;BoardRequest *q=calloc(1,sizeof(*q));if(!q)return 0;
    q->op=key?BOARD_SSH_KEY:'a';snprintf(q->username,sizeof(q->username),"%s",name);
    snprintf(q->password,sizeof(q->password),"%s",proof);c->app.core.now=now_ms();
    int accepted=session_submit(&c->app,q)==SESSION_OPERATION_ACCEPTED;
    OPENSSL_cleanse(q,sizeof(*q));free(q);return accepted;
}
static unsigned online(void *context) {
    (void)context; unsigned n=0;
    for(int i=0;i<SLOTS;i++) if(connections[i].fd!=INVALID) n++;
    return n;
}

static void notify_all(MessageId id,uint32_t number,const char *body) {
    for(int i=0;i<SLOTS;i++)if(connections[i].fd!=INVALID) {
        session_notify(&connections[i].app,id,number,body);
    }
}
static void service_result(BoardResult *r) {
    if(r->service_action=='J') {
        if(!r->service_number) {
            if(r->structured) {
                DoorListing listing={0};doors_listing(r->service_next,&listing);
                OPERATION_PAGE(r,DOORS,doors,listing);
            }
            doors_list(r->service_next,r->text,sizeof(r->text));return;
        }
        if(r->slot<0 || r->slot>=SLOTS) return;
        Connection *c=&connections[r->slot];Session *s=&c->app;
        if(c->fd==INVALID || s->core.id!=r->session || !s->core.user || s->core.closing || !s->connection.binary_allowed || c->door) { door_launch_diagnostic("session-admission",0);r->feedback.door_error=DOOR_REQUEST_DENIED;(r->feedback.failed=1,strcpy(r->text,"ERR game unavailable\r\n"));return; }
        c->door=door_start(r->service_number,r->service_next,s->core.user,s->core.username,s->connection.columns,s->connection.rows,now_ms(),s->core.id,s->core.user_epoch,&r->feedback.door_error);
        if(!c->door) {
            r->feedback.failed=1;
            strcpy(r->text,r->feedback.door_error==DOOR_REQUEST_FULL?"ERR game busy\r\n":"ERR game unavailable\r\n");return;
        }
        s->connection.door=1;c->used=0;terminal_clear(&c->terminal);session_scan_pause(s);
        if(!session_notice(s,SESSION_NOTICE_DOOR,0,0,0,r->text,sizeof(r->text)))session_abort(s);
        return;
    }
    if(r->all_accounts_changed)for(int i=0;i<SLOTS;i++)if(connections[i].fd!=INVALID && connections[i].app.core.id!=r->session)session_revoke(&connections[i].app);
    if(r->access_refresh)for(int i=0;i<SLOTS;i++)if(connections[i].fd!=INVALID) {
        Session *s=&connections[i].app;
        session_access_refresh(s);
    }
    if((!r->service_action || strchr("GgM",r->service_action)) && !r->board_refresh)return;
    if(r->service_action=='u') { userlog_table(r->service_number,r->service_next,!strcmp(r->service_text,"ALL"),now_ms(),r->text,sizeof(r->text));return; }
    if(r->service_action=='U' || r->service_action=='V') {
        userlog_report(r->service_number,r->service_next,!strcmp(r->service_text,"ALL"),r->service_action=='V',now_ms(),r->text,sizeof(r->text));return;
    }
    if(r->service_action=='b' || r->board_refresh) {
        for(int i=0;i<SLOTS;i++)if(connections[i].fd!=INVALID) {
            session_board_refresh(&connections[i].app);
        }
        return;
    }
    if(r->service_action=='L') {
        ConnectionSummary rows[SLOTS];size_t count=0;
        for(int i=0;i<SLOTS;i++) {
            Connection *c=&connections[i];if(c->fd==INVALID)continue;
            ConnectionState state=connection_state(c);ConnectionSummary *row=&rows[count++];
            memset(row,0,sizeof(*row));row->id=c->app.core.id;row->mode=connection_mode(&state);
            row->telnet=c->ssh?2:c->terminal.mode==TERMINAL_TELNET;
            snprintf(row->login,sizeof(row->login),"%s",c->app.core.user?c->app.core.username:"guest");
            snprintf(row->peer,sizeof(row->peer),"%s",c->peer);
            row->elapsed=connection_elapsed(now_ms(),c->clock.started)/1000;
            row->idle=connection_elapsed(now_ms(),c->clock.last_input)/1000;
            row->rx=c->rx_bytes;row->tx=c->tx_bytes;row->queued=c->queued;
            row->columns=c->app.connection.columns;row->rows=c->app.connection.rows;
        }
        if(!connection_protocol_list(rows,count,r->text,sizeof(r->text)))(r->feedback.failed=1,strcpy(r->text,"ERR connection report\r\n"));
    } else if(r->service_action=='A') { notify_all(MSG_ADMIN_NOTICE,0,r->service_text);strcpy(r->text,"ANNOUNCE SENT\r\n"); }
    else if(r->service_action=='K') {
        int found=0;for(int i=0;i<SLOTS;i++)if(connections[i].fd!=INVALID && connections[i].app.core.id==r->service_number) { drop(&connections[i]);found=1;break; }
        r->feedback.failed=!found;snprintf(r->text,sizeof(r->text),found?"DISCONNECTED %u\r\n":"ERR no connection\r\n",r->service_number);
    } else if(r->service_action=='C') { connection_shutdown_cancel(&shutdown_state);notify_all(MSG_SHUTDOWN_CANCELLED,0,"");strcpy(r->text,"SHUTDOWN CANCELLED\r\n"); }
    else if(r->service_action=='S') {
        connection_shutdown_schedule(&shutdown_state,now_ms(),r->service_number,r->service_text);
        notify_all(MSG_SHUTDOWN_NOTICE,r->service_number,shutdown_state.reason);snprintf(r->text,sizeof(r->text),"SHUTDOWN SCHEDULED %u\r\n",r->service_number);
    }
}
static int zbytes(void *context,const unsigned char *bytes,size_t size) {
    Connection *c=context;return terminal_write(&c->terminal,bytes,size);
}
static void zadvance(Connection *c,uint64_t now) {
    Session *s=&c->app;
    if(!s->core.active || s->core.closing) { drop(c);return; }
    /* The helper and zhost watchdog enforce inactivity, not total receive time. */
    terminal_set_binary(&c->terminal,1);
    if(!terminal_binary_ready(&c->terminal)) { drop(c);return; }
    if(c->zend) {
        if(c->used) { OPENSSL_cleanse(c->input,c->used);c->used=0;c->zquiet=now; }
        if(now-c->zend>2000 && now-c->zquiet<500) { drop(c);return; }
        if(now-c->zquiet>=500) {
            c->zend=0;terminal_set_binary(&c->terminal,0);
            session_zmodem_finish(s,c->zresult,c->zid,c->zsize,c->zrecovery);
        }
        return;
    }
    if(!c->zhost) {
        if(next_zgeneration==INT_MAX) { drop(c);return; }
        c->zgeneration=next_zgeneration++;c->zstarted=now;
        c->zhost=zhost_start(zhelper,s->connection.zmodem_receive,s->connection.zmodem_id,s->core.username,s->connection.zmodem_text,service_port,now,s->connection.zmodem_protocol);
        if(!c->zhost) { c->zresult=0;c->zid=c->zsize=0;c->zrecovery=0;c->zend=c->zquiet=now;return; }
    }
    if(c->queued<=OUT_CAP/2) {
        int done=zhost_pump(c->zhost,zbytes,c,now);
        uint64_t bytes,total;
        if(zhost_progress(c->zhost,&bytes,&total)) {
            char text[192];unsigned percent=total?(unsigned)(bytes*100/total):100;
            if(!session_notice(s,SESSION_NOTICE_PROGRESS,percent,bytes,total,text,sizeof(text))) { drop(c);return; }
            if(!zbytes(c,(const unsigned char *)text,strlen(text))) { drop(c);return; }
        }
        if(done) {
            zhost_receipt(c->zhost,&c->zid,&c->zsize,&c->zrecovery);
            c->zresult=done>0;c->zend=c->zquiet=now;zhost_stop(c->zhost);c->zhost=NULL;
            OPENSSL_cleanse(c->input,c->used);c->used=0;return;
        }
    }
    const char *line=zhost_request(c->zhost);
    if(line) {
        BoardRequest q={0};q.slot=s->core.slot;q.session=s->core.id;q.user=s->core.user;q.user_epoch=s->core.user_epoch;q.zcontrol=c->zgeneration;memcpy(q.vault_grants,s->core.vault_grants,sizeof(q.vault_grants));q.scope=(int)s->connection.zmodem_protocol+1;
        if(!strcmp(line,"ADMIN"))q.op='z';
        else if(!strncmp(line,"MFILE",5)) { q.op=BOARD_MAIL_TRANSFER;snprintf(q.body,sizeof(q.body),"%s",line+1); }
        else if(transfer_line_command(line)) { if(strlen(line)>=sizeof(q.body)) { drop(c);return; }q.op='$';snprintf(q.body,sizeof(q.body),"%s",line); }
        else if(!strncmp(line,"FILE ",5)) {
            char *end;unsigned long id=strtoul(line+5,&end,10);
            if(end==line+5 || *end || !id || id>VAULT_ID_MAX) { drop(c);return; }
            q.op='Y';q.action=VAULT_CMD_INFO;q.number=(uint32_t)id;
        } else { drop(c);return; }
        if(board_submit(&q))zhost_submitted(c->zhost);
    }
    size_t consumed=0;
    while(consumed<c->used && zhost_room(c->zhost)) {
        unsigned char byte;int result=terminal_binary_receive(&c->terminal,c->input[consumed++],&byte);
        if(result<0 || (result && !zhost_input(c->zhost,byte))) { drop(c);return; }
    }
    memmove(c->input,c->input+consumed,c->used-consumed);c->used-=consumed;
}
/* Decode only up to one completed line, then let the application change input
 * policy before decoding any following bytes (including a pipelined password). */
static void door_advance(Connection *c,uint64_t now) {
    Session *s=&c->app;
    if(!s->core.active || s->core.closing) { drop(c);return; }
    terminal_set_binary(&c->terminal,1);int done=0;size_t used=0;
    unsigned old_width=c->terminal.telnet.width,old_height=c->terminal.telnet.height;
    while(used<c->used && door_room(c->door)) {
        unsigned char byte;int rc=terminal_binary_receive(&c->terminal,c->input[used++],&byte);
        if(rc<0) { drop(c);return; }if(!rc)continue;connection_input(&c->clock,now);
        if(byte==29) { done=1;break; }if(!door_input(c->door,byte)) { done=-1;break; }
    }
    memmove(c->input,c->input+used,c->used-used);c->used-=used;
    if(c->terminal.mode==TERMINAL_TELNET &&
       (old_width!=c->terminal.telnet.width || old_height!=c->terminal.telnet.height)) {
        session_dimensions(s,c->terminal.telnet.width,c->terminal.telnet.height);
        /* A vanished PTY must not kill the BBS session: door_pump handles exit. */
        if(!done)(void)door_resize(c->door,s->connection.columns,s->connection.rows);
    }
    if(!done && c->queued<=OUT_CAP/2)done=door_pump(c->door,zbytes,c,now);
    if(done) {
        door_stop(c->door);c->door=NULL;s->connection.door=0;c->used=0;terminal_clear(&c->terminal);terminal_set_binary(&c->terminal,0);
        session_door_finish(s);
    }
}
static void advance(Connection *c,uint64_t now) {
    Session *s=&c->app;
    if(s->connection.door) { door_advance(c,now);return; }
    if(s->connection.zmodem) { connection_input(&c->clock,now);zadvance(c,now);return; }
    if(session_auto_read(s) && c->used) {
        size_t consumed=0;int stop=0;
        while(consumed<c->used) {
            int rc=terminal_receive(&c->terminal,c->input[consumed++],0);
            if(rc<0) { drop(c);return; }
            if(rc>0 || c->terminal.size) { stop=1;break; }
        }
        memmove(c->input,c->input+consumed,c->used-consumed);c->used-=consumed;
        if(stop) { connection_input(&c->clock,now);terminal_clear(&c->terminal);c->used=0;session_scan_stop(s); }
    }
    if(!session_auto_read(s) || c->queued<=OUT_CAP/2)session_tick(s,now);
    if(!s->core.active) { drop(c); return; }
    if(!session_ready(s) || c->queued>OUT_CAP/2) return;
    size_t consumed=0;
    terminal_set_binary(&c->terminal,s->connection.binary);
    while(consumed<c->used) {
        if(s->connection.binary) {
            unsigned char byte;int decoded=terminal_binary_receive(&c->terminal,c->input[consumed++],&byte);
            if(decoded<0) { drop(c);return; }
            int complete=decoded?session_binary_byte(s,byte,now):0;
            if(complete<0 || !s->core.active) { drop(c);return; }
            if(complete)break;
            continue;
        }
        unsigned old_width=c->terminal.telnet.width,old_height=c->terminal.telnet.height;
        size_t old_size=c->terminal.size;
        c->terminal.uppercase=session_uppercase(s,c->terminal.line,c->terminal.size);
        c->terminal.keys=session_keys(s);
        int result=terminal_receive(&c->terminal,c->input[consumed++],session_secret(s));
        if(result>0 || c->terminal.size!=old_size) { connection_input(&c->clock,now); }
        if(c->terminal.mode==TERMINAL_TELNET && (old_width!=c->terminal.telnet.width || old_height!=c->terminal.telnet.height))
            session_dimensions(s,c->terminal.telnet.width,c->terminal.telnet.height);
        if(result<0) { drop(c); return; }
        if(session_secret(s) && c->terminal.mode==TERMINAL_TELNET && !telnet_echo(&c->terminal.telnet)) { drop(c); return; }
        if(result>0) {
            s->connection.binary_allowed=terminal_binary_ready(&c->terminal);
            if(result==TERMINAL_KEY)session_key(s,c->terminal.key,now);
            else session_line(s,c->terminal.line,c->terminal.size,now);
            terminal_clear(&c->terminal);
            /* Do not accept passwords from Telnet clients declining remote echo. */
            if(session_secret(s) && c->terminal.mode==TERMINAL_TELNET && !telnet_echo(&c->terminal.telnet)) session_abort(s);
            break;
        }
    }
    memmove(c->input,c->input+consumed,c->used-consumed); c->used-=consumed;
    OPENSSL_cleanse(c->input+c->used,CAP-c->used);
    if(!s->core.active) drop(c);
}
int main(int argc,char **argv) {
    const char *program=argv[0];
    const char *doors_path=NULL,*ssh_keys=NULL;
    LnSshConfig ssh_config={.bind="127.0.0.1",.attempts=6,.timeout=60};
    LnSshServer *ssh_server=NULL;
    const char *help_directory="help";int help_required=0;
    const char *user_log=NULL;
    const char *chat_log=NULL;char chat_default[4096];
    int structured_database=0;const char *limits_path=NULL,*database_path=NULL;
    const char *timezone="UTC",*site_info="sysinfo.txt",*site_bye="goodbye.txt";
    const char *ui_policy="FIXED",*ui_default="rtbbs",*ui_allowed="rtbbs";
    SessionUiConfig ui_config=session_ui_default();
    const char *login_directory=".";
    const char *id_prefix="u",*id_first="";unsigned id_digits=6;
    const char *banner_path="banner.txt",*menu_directory="menus",*bind_address="127.0.0.1",*vault_directory=NULL; int menu_required=0;
    while(argc>1 && !strncmp(argv[1],"--",2)) {
        if((!strcmp(argv[1],"--max-articles") || !strcmp(argv[1],"--max-mail") || !strcmp(argv[1],"--max-user-posts")) && argc>2) {
            char *tail;unsigned long n=strtoul(argv[2],&tail,10);
            if(!*argv[2] || *argv[2]<'0' || *argv[2]>'9' || *tail || n>CONTENT_ID_MAX) { fprintf(stderr,"invalid count limit\n");return 2; }
            if(!strcmp(argv[1],"--max-articles"))limit_articles=(uint32_t)n;
            else if(!strcmp(argv[1],"--max-mail"))limit_mail=(uint32_t)n;
            else limit_user_posts=(uint32_t)n;
            argc-=2;argv+=2;
        }
        else if((!strcmp(argv[1],"--ssh-port") || !strcmp(argv[1],"--ssh-attempts") || !strcmp(argv[1],"--ssh-timeout")) && argc>2) {
            char *tail;unsigned long n=strtoul(argv[2],&tail,10);
            if(argv[2][0]<'0' || argv[2][0]>'9' || *tail || n>65535)return 2;
            if(!strcmp(argv[1],"--ssh-port"))ssh_config.port=(unsigned)n;
            else if(!strcmp(argv[1],"--ssh-attempts"))ssh_config.attempts=(unsigned)n;
            else ssh_config.timeout=(unsigned)n;
            argc-=2;argv+=2;
        }
        else if(!strcmp(argv[1],"--ssh-bind") && argc>2) { ssh_config.bind=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--ssh-host-key") && argc>2) { ssh_config.host_key=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--ssh-member-keys") && argc>2) { ssh_keys=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--database-structured") && argc>2) { structured_database=1;database_path=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--database") && argc>2) { database_path=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--limits") && argc>2) { limits_path=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--sysinfo") && argc>2) { site_info=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--goodbye") && argc>2) { site_bye=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--timezone") && argc>2) { timezone=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--door-isolation-socket") && argc>2) { if(!doors_isolation_config(argv[2])) { fprintf(stderr,"Door isolation unsupported or invalid socket\n");return 2; }argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--doors") && argc>2) { doors_path=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--gather-count") && argc>2) {
            char *end;unsigned long n=strtoul(argv[2],&end,10);if(*argv[2]<'0' || *argv[2]>'9' || *end || n>1000) { fprintf(stderr,"gather-count: 0..1000 required\n");return 2; }gather_limit=(unsigned)n;argc-=2;argv+=2;
        }
        else if(!strcmp(argv[1],"--rtbbs-member-policy")) { members_rtbbs_policy=1;argc--;argv++; }
        else if(!strcmp(argv[1],"--userlog-public")) { userlog_public=1;argc--;argv++; }
        else if((!strcmp(argv[1],"--userlog-keep") || !strcmp(argv[1],"--time-warning") || !strcmp(argv[1],"--time-grace")) && argc>2) {
            char *end;unsigned long n=strtoul(argv[2],&end,10);if(*argv[2]<'0' || *argv[2]>'9' || *end || n>1000000)return 2;
            if(!strcmp(argv[1],"--userlog-keep"))userlog_keep=(unsigned)n;else if(!strcmp(argv[1],"--time-warning"))policy.warning_seconds=(unsigned)n;else policy.grace_seconds=(unsigned)n;argc-=2;argv+=2;
        }
        else if(!strcmp(argv[1],"--level-seconds") && argc>2) {
            unsigned level,seconds;int n=0;if(sscanf(argv[2],"%u:%u%n",&level,&seconds,&n)!=2 || argv[2][n] || level>5 || seconds>86400)return 2;policy.level_seconds[level]=seconds;argc-=2;argv+=2;
        }
        else if(!strcmp(argv[1],"--user-log") && argc>2) { user_log=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--chat-log") && argc>2) { chat_log=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--disable") && argc>2) {
            if(!board_disable_feature(argv[2])) { fprintf(stderr,"disable: chat|mail|vault required\n");return 2; }
            argc-=2;argv+=2;
        }
        else if(!strcmp(argv[1],"--registration-form") && argc>2) {
            if(strcmp(argv[2],"simple") && strcmp(argv[2],"detailed")) { fprintf(stderr,"registration-form: simple|detailed required\n");return 2; }
            members_registration_form(!strcmp(argv[2],"detailed"));argc-=2;argv+=2;
        }
        else if(!strcmp(argv[1],"--id-first") && argc>2) { id_first=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--id-prefix") && argc>2) { id_prefix=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--id-digits") && argc>2) { if(argv[2][0]<'1' || argv[2][0]>'8' || argv[2][1])return 2;id_digits=(unsigned)(argv[2][0]-'0');argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--id-mode") && argc>2) {
            if(strcmp(argv[2],"free") && strcmp(argv[2],"auto")) { fprintf(stderr,"id-mode: free|auto required\n");return 2; }
            members_id_mode(!strcmp(argv[2],"auto"));argc-=2;argv+=2;
        }
        else if(!strcmp(argv[1],"--registration") && argc>2) {
            int mode=!strcmp(argv[2],"open")?0:!strcmp(argv[2],"approval")?1:!strcmp(argv[2],"closed")?2:-1;
            if(mode<0) { fprintf(stderr,"registration: open|approval|closed required\n");return 2; }
            members_registration(mode);argc-=2;argv+=2;
        }
        else if(!strcmp(argv[1],"--idle-seconds") && argc>2) {
            char *tail;unsigned long n=strtoul(argv[2],&tail,10);
            if(!*argv[2] || *tail || argv[2][0]=='-' || argv[2][0]=='+' || n>86400) { fprintf(stderr,"idle-seconds: use 0..86400\n");return 2; }
            policy.idle_seconds=(uint32_t)n;argc-=2;argv+=2;
        }
        else if(!strcmp(argv[1],"--zmodem-helper") && argc>2) { zhelper=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--admins") && argc>2) { admins_path=argv[2]; argc-=2; argv+=2; }
        else if(!strcmp(argv[1],"--link-accounts") && argc>2) { link_accounts_path=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--vault-dir") && argc>2) { vault_directory=argv[2]; argc-=2; argv+=2; }
        else if(!strcmp(argv[1],"--bind") && argc>2) { bind_address=argv[2]; argc-=2; argv+=2; }
        else if(!strcmp(argv[1],"--login-text-dir") && argc>2) { login_directory=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--banner") && argc>2) { banner_path=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--help-dir") && argc>2) { help_directory=argv[2];help_required=1;argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--menu-dir") && argc>2) { menu_directory=argv[2]; menu_required=1; argc-=2; argv+=2; }
        else if((!strcmp(argv[1],"--ui-policy") || !strcmp(argv[1],"--ui-default") || !strcmp(argv[1],"--ui-allow")) && argc>2) {
            if(!strcmp(argv[1],"--ui-policy"))ui_policy=argv[2];
            else if(!strcmp(argv[1],"--ui-default"))ui_default=argv[2];
            else ui_allowed=argv[2];
            argc-=2;argv+=2;
        }
        else if(!strcmp(argv[1],"--menu")) { menu_mode=1; argc--; argv++; }
        else if(!strcmp(argv[1],"--telnet")) { terminal_mode=TERMINAL_TELNET; argc--; argv++; }
        else if(!strcmp(argv[1],"--lang") && argc>2 && language_parse(argv[2],&default_language)) { argc-=2; argv+=2; }
        else { fprintf(stderr,"invalid option; use --zmodem-helper FILE, --vault-dir DIR, --bind IPv4, --menu-dir DIR, --menu, --telnet, --lang ja|en and/or --admins FILE\n"); return 2; }
    }
    if(!session_ui_configure(&ui_config,ui_policy,ui_default,ui_allowed)) { fprintf(stderr,"invalid UI policy/default/allowlist\n");return 2; }
    char *end=NULL;
    if(doors_path && !door_lock_init(program)) { fprintf(stderr,"Door lock helper missing or unsafe\n");return 2; }
    if(!doors_config(doors_path)) { fprintf(stderr,"invalid doors configuration\n");return 2; }
    long port=(argc>=2 && argc<=5) ? strtol(argv[1],&end,10) : -1;
    if(port<0 || port>65535 || !end || end==argv[1] || *end) {
        fprintf(stderr,"usage: runtime [--ui-policy FIXED|MEMBERS|ALL] [--ui-default NAME] [--ui-allow NAME,...] [--ssh-port PORT --ssh-host-key FILE] [--ssh-bind IPv4] [--ssh-member-keys FILE] [--ssh-attempts N] [--ssh-timeout SECONDS] [--database FILE | --database-structured FILE] [--level-seconds LEVEL:SECONDS] [--time-warning N] [--time-grace N] [--userlog-public] [--userlog-keep N] [--idle-seconds N] [--user-log FILE|-] [--gather-count N] [--chat-log FILE|-] [--max-articles N] [--max-mail N] [--max-user-posts N] [--banner FILE] [--disable chat|mail|vault] [--registration open|approval|closed] [--id-mode free|auto] [--id-prefix PREFIX] [--id-digits 1..8] [--id-first NAME] [--rtbbs-member-policy] [--login-text-dir DIR] [--registration-form simple|detailed] [--door-isolation-socket FILE] [--doors FILE] [--link-accounts FILE] [--zmodem-helper FILE] [--vault-dir DIR] [--bind IPv4] [--menu] [--menu-dir DIR] [--help-dir DIR] [--telnet] [--lang ja|en] [--admins FILE] PORT [BOARD_FILE [MEMBERS_FILE [CATALOG_FILE]]] (default bind: 127.0.0.1)\n"); return 2;
    }
#ifdef _WIN32
    WSADATA data; if(WSAStartup(MAKEWORD(2,2),&data)) return 1;
#else
    signal(SIGPIPE,SIG_IGN);
#endif
    struct sockaddr_in addr; memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;
    if(inet_pton(AF_INET,bind_address,&addr.sin_addr)!=1) {
        fprintf(stderr,"invalid --bind address: use a numeric IPv4 address\n"); return 2;
    }
    addr.sin_port=htons((unsigned short)port);
    if(zhelper && !zhost_supported(zhelper)) { fprintf(stderr,"zmodem: requires an executable absolute helper path on POSIX\n");return 2; }
    if(!menu_load(menu_directory,menu_required) || !help_load(help_directory,help_required)) return 2;
    if(!members_id_format(id_prefix,id_digits) || !members_first_id(id_first)) { fprintf(stderr,"invalid ID format\n");return 2; }
    if(database_path) { if(!(structured_database?storage_init_structured(database_path):storage_init(database_path))) { fprintf(stderr,"database: cannot open/lock/validate\n");return 2; }atexit(storage_shutdown); }
    if(limits_path && !ln_limits_load(limits_path)) { fprintf(stderr,"limits: invalid configuration\n");return 2; }
    if(strcmp(timezone,"UTC")) {
        char zonefile[512];if(!*timezone || *timezone=='/' || strstr(timezone,"..") || strspn(timezone,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/_+-")!=strlen(timezone) || snprintf(zonefile,sizeof(zonefile),"/usr/share/zoneinfo/%s",timezone)>=(int)sizeof(zonefile))return 2;
        FILE *zf=fopen(zonefile,"rb");if(!zf) { fprintf(stderr,"timezone: unknown IANA zone\n");return 2; }char magic[4];int valid=fread(magic,1,4,zf)==4 && !memcmp(magic,"TZif",4);fclose(zf);if(!valid)return 2;
    }
#ifdef _WIN32
    if(_putenv_s("TZ",timezone))return 2;_tzset();
#else
    if(setenv("TZ",timezone,1))return 2;
    tzset();
#endif
    if(!site_load(site_info,site_bye)) { fprintf(stderr,"site messages: invalid UTF-8 or unreadable file\n");return 2; }
    if(!login_text_load(login_directory)) { fprintf(stderr,"invalid login text files\n");return 2; }
    if(!banner_load(banner_path)) { fprintf(stderr,"banner: invalid UTF-8 or unreadable file\n");return 2; }
    if(vault_directory && (argc<4 || !vault_bridge_open(vault_directory))) { fprintf(stderr,"vault: requires initialized directory and board/member files\n"); return 2; }
    signal(SIGINT,stop); signal(SIGTERM,stop);
    if(admins_path && argc<4) { fprintf(stderr,"admins: requires board and member files\n"); return 2; }
    if(link_accounts_path && (argc<4 || !board_link_config(link_accounts_path))) { fprintf(stderr,"link: invalid board/account allowlist\n");return 2; }
    if(ssh_config.port) {
        struct in_addr ssh_addr;
        if(argc<4 || !ssh_config.host_key || inet_pton(AF_INET,ssh_config.bind,&ssh_addr)!=1) { fprintf(stderr,"SSH requires members, a host key and numeric IPv4 bind\n");return 2; }
        ssh_config.public_keys=ssh_keys!=NULL;members_keys_path(ssh_keys);
        ssh_server=ln_ssh_server(&ssh_config);
        if(!ssh_server) { fprintf(stderr,"SSH setup failed: check SSH-enabled build, host key permissions and listener settings\n");return 2; }
    }
    if(argc>=3) { if(!board_start_config(argv[2],argc>=4?argv[3]:NULL,argc==5?argv[4]:NULL,admins_path)) return 1; board_enabled=1; member_mode=argc>=4; }
    if(board_enabled && (!user_log || strcmp(user_log,"-")) && !(user_log?userlog_open_file(user_log):userlog_open(argc>=4?argv[3]:argv[2]))) { fprintf(stderr,"userlog: cannot open/lock/validate\n");board_stop();return 1; }
    if(!chat_log && argc>=4) { int n=snprintf(chat_default,sizeof(chat_default),"%s.chatlog",argv[3]);if(n<0 || (size_t)n>=sizeof(chat_default))return 2;chat_log=chat_default; }
    if(chat_log && strcmp(chat_log,"-") && board_feature_enabled("chat") && !chat_log_open(chat_log)) { fprintf(stderr,"chat log: cannot open/lock/validate\n");board_stop();return 1; }
    for(int i=0;i<SLOTS;i++) connections[i].fd=INVALID;
    socket_t listener=socket(AF_INET,SOCK_STREAM,0);
    unsigned requested_port=(unsigned)ntohs(addr.sin_port);
    if(listener==INVALID)return listener_error(listener,"socket",bind_address,requested_port);
#ifndef _WIN32
    /* Rebind after our closed connections enter TIME_WAIT. Do not use
     * SO_REUSEPORT: a second live server must still fail to bind. Winsock's
     * SO_REUSEADDR has different exclusivity semantics, so leave it unchanged. */
    int reuse_address=1;
    if(setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse_address,sizeof(reuse_address)))
        return listener_error(listener,"setsockopt(SO_REUSEADDR)",bind_address,requested_port);
#endif
    if(nonblock(listener))return listener_error(listener,"nonblock",bind_address,requested_port);
    if(bind(listener,(struct sockaddr *)&addr,sizeof(addr)))
        return listener_error(listener,"bind",bind_address,requested_port);
    if(listen(listener,CONNECTION_MAX))return listener_error(listener,"listen",bind_address,requested_port);
#ifdef _WIN32
    int addrlen=sizeof(addr);
#else
    socklen_t addrlen=sizeof(addr);
#endif
    if(getsockname(listener,(struct sockaddr *)&addr,&addrlen))
        return listener_error(listener,"getsockname",bind_address,requested_port);
    service_port=(unsigned)ntohs(addr.sin_port);
    printf("LISTEN %s %u\n",bind_address,service_port); fflush(stdout);
    if(ssh_server) { printf("SSH LISTEN %s %u\n",ssh_config.bind,ssh_config.port);fflush(stdout); }
    int result=0;
    while(!stopping) {
        uint64_t clock=now_ms();
        ShutdownDecision shutdown=connection_shutdown_step(&shutdown_state,clock);
        if(shutdown==SHUTDOWN_DRAIN) {
            notify_all(MSG_SHUTDOWN_STOPPING,0,"");
            for(int i=0;i<SLOTS;i++)if(connections[i].fd!=INVALID) {
                Connection *c=&connections[i];if(c->app.connection.zmodem || c->app.connection.binary)drop(c);
                else { session_tick(&c->app,clock);c->app.core.closing=1; }
            }
        }
        if(shutdown==SHUTDOWN_STOP) { stopping=1;break; }
        zhost_reap(now_ms());
        BoardResult completion;
        board_completion_drain();
        while(board_poll(&completion)) {
            service_result(&completion);
            if(completion.zcontrol) {
                if(completion.slot>=0 && completion.slot<SLOTS) {
                    Connection *c=&connections[completion.slot];
                    if(c->fd!=INVALID && c->app.core.id==completion.session && c->zhost && c->zgeneration==completion.zcontrol) {
                        session_transfer_receipt(&c->app,completion.text);
                        if(completion.revoked || !zhost_answer(c->zhost,completion.text))drop(c);
                    }
                }
                continue;
            }
            if(completion.mail_notify_user)for(int i=0;i<SLOTS;i++)
                if(connections[i].fd!=INVALID)session_mail_notify(&connections[i].app,completion.mail_notify_user,completion.mail_notify_deny,completion.mail_notify_revision);
            /* A durable send notifies all recipient sessions, even if its sender disconnected. */
            if(completion.mail_delivery)for(int i=0;i<SLOTS;i++)
                if(connections[i].fd!=INVALID)session_mail_arrived(&connections[i].app,completion.mail_delivery);
            /* Committed moderation commands apply even after requester disconnect. */
            if(completion.chat_kick_user)for(int i=0;i<SLOTS;i++) {
                Connection *c=&connections[i];Session *s=&c->app;
                if(c->fd!=INVALID && s->core.active && !s->core.closing && s->core.user==completion.chat_kick_user && (session_ui_snapshot(s).chatting || session_ui_snapshot(s).chat_pending)) {
                    c->used=0;c->queued=0;OPENSSL_cleanse(c->input,sizeof(c->input));terminal_clear(&c->terminal);
                    session_chat_kick(s);completion.chat_kick_count++;
                }
            }
            if(completion.chat_user)chat_apply_preferences(completion.chat_user,&completion.chat_preferences);
            /* Apply account events even if the administrator has disconnected.
             * Epochs also reject delayed pre-suspension authentication results. */
            if(completion.account_user2 && completion.account_user2<=MEMBER_MAX) {
                uint32_t user=completion.account_user2;
                if(account_epochs[user]<completion.account_epoch2)account_epochs[user]=completion.account_epoch2;
                for(int i=0;i<SLOTS;i++) {
                    Connection *c=&connections[i];if(c->fd!=INVALID && c->app.core.user==user && c->app.core.user_epoch<account_epochs[user]) {
                        c->used=0;c->queued=0;OPENSSL_cleanse(c->input,sizeof(c->input));terminal_clear(&c->terminal);session_revoke(&c->app);
                    }
                }
            }
            if(completion.account_user && completion.account_user<=MEMBER_MAX) {
                uint32_t user=completion.account_user;
                if(completion.password_changed && completion.slot>=0 && completion.slot<SLOTS) {
                    Session *owner=&connections[completion.slot].app;
                    (void)session_core_password_epoch(&owner->core,completion.session,user,completion.account_epoch);
                }
                if(account_epochs[user]<completion.account_epoch) account_epochs[user]=completion.account_epoch;
                for(int i=0;i<SLOTS;i++) {
                    Connection *c=&connections[i];
                    if(c->fd!=INVALID && c->app.core.user==user && c->app.core.user_epoch<account_epochs[user]) {
                        c->used=0; c->queued=0; OPENSSL_cleanse(c->input,sizeof(c->input));
                        terminal_clear(&c->terminal); session_revoke(&c->app);
                    }
                }
            }
            if(completion.slot>=0 && completion.slot<SLOTS) {
                Connection *c=&connections[completion.slot];
                if(c->ssh && ln_ssh_auth_pending(c->ssh) && c->app.core.id==completion.session) {
                    OPENSSL_cleanse(c->output,c->queued);c->queued=0;
                }
                if(c->app.core.id==completion.session && completion.auth>0 && completion.auth<=MEMBER_MAX && completion.user_epoch<account_epochs[completion.auth]) {
                    c->used=0; c->queued=0; OPENSSL_cleanse(c->input,sizeof(c->input));
                    terminal_clear(&c->terminal); session_revoke(&c->app);
                } else session_result(&c->app,&completion,now_ms());
                if(c->ssh && ln_ssh_auth_pending(c->ssh)) {
                    int accepted=completion.auth>0 && c->app.core.user==(uint32_t)completion.auth && !c->app.core.closing;
                    fprintf(stderr,"SSH auth %s slot=%d\n",accepted?"accepted":"rejected",completion.slot);
                    if(!ln_ssh_auth_result(c->ssh,accepted))drop(c);
                }
            }
        }
        doors_reap();
        pollfd_t fds[SLOTS+3]; int map[SLOTS+3]; unsigned count=1;
        memset(fds,0,sizeof(fds));
        fds[0].fd=listener; fds[0].events=POLLIN;
        if(ssh_server) { fds[count].fd=ln_ssh_listener(ssh_server);fds[count].events=POLLIN;map[count++]=-2; }
        int notification=board_completion_fd();
        if(notification>=0) { fds[count].fd=notification;fds[count].events=POLLIN;map[count++]=-1; }
        uint64_t now=now_ms(); int timeout=50;
        for(int i=0;i<SLOTS;i++) {
            Connection *c=&connections[i]; if(c->fd==INVALID) continue;
            if(!userlog_update(i,c->app.core.user,now) || !userlog_maintain()) { fprintf(stderr,"userlog: write failed\n");result=1;stopping=1;break; }
            ConnectionState state=connection_state(c);
            ConnectionDecision decision=connection_time_check(&policy,&c->clock,&state,now);
            if(decision==CONNECTION_DROP) { drop(c);continue; }
            if(decision==CONNECTION_WARNING || decision==CONNECTION_GRACE) {
                char message[900];
                if(!session_notice(&c->app,decision==CONNECTION_GRACE?SESSION_NOTICE_TIME_GRACE:SESSION_NOTICE_TIME_WARNING,0,0,0,message,sizeof(message))) { drop(c);continue; }
                if(!app_write(c,message,strlen(message))) { drop(c);continue; }
            }
            if(connection_idle_drained(&c->clock,now)) { drop(c);continue; }
            connection_busy_activity(&c->clock,&state,now);
            if(c->ssh) {
                if(!ln_ssh_pump(c->ssh,now)) { drop(c);continue; }
                unsigned columns,rows;
                if(ln_ssh_dimensions(c->ssh,&columns,&rows)) {
                    session_dimensions(&c->app,columns,rows);
                    if(c->door)(void)door_resize(c->door,c->app.connection.columns,c->app.connection.rows);
                }
                if(ln_ssh_ready(c->ssh)) {
                    if(c->queued) {
                        int n=ln_ssh_write(c->ssh,c->output,c->queued);
                        if(n<0) { drop(c);continue; }
                        c->tx_bytes+=(unsigned)n;c->queued-=(size_t)n;memmove(c->output,c->output+n,c->queued);
                    }
                    if(c->used<CAP && c->queued<=OUT_CAP/2) {
                        size_t room=CAP-c->used;if(room>512)room=512;
                        int n=ln_ssh_read(c->ssh,c->input+c->used,room);
                        if(n<0) { drop(c);continue; }
                        c->used+=(size_t)n;c->rx_bytes+=(unsigned)n;
                    }
                    advance(c,now);
                }
            } else advance(c,now);
            if(c->fd!=INVALID) {
                state=connection_state(c);decision=connection_idle_check(&policy,&c->clock,&state,now);
                if(decision==CONNECTION_CLOSE) {
                    char msg[900];if(!session_notice(&c->app,SESSION_NOTICE_IDLE_CLOSED,0,0,0,msg,sizeof(msg))) { drop(c);continue; }
                    if(!app_write(c,msg,strlen(msg))) { drop(c);continue; }
                    c->app.core.closing=1;
                } else if(decision==CONNECTION_WARNING) {
                    char msg[900];if(!session_notice(&c->app,SESSION_NOTICE_IDLE_WARNING,0,0,0,msg,sizeof(msg))) { drop(c);continue; }if(!app_write(c,msg,strlen(msg))) { drop(c);continue; }
                }
            }
            if(c->fd!=INVALID && !userlog_update(i,c->app.core.user,now)) { result=1;stopping=1;break; }
            if(c->fd==INVALID) continue;
            Session *s=&c->app;
            if(s->core.closing && !c->queued) { drop(c); continue; }
            if(c->used==CAP && !s->connection.zmodem) { drop(c); continue; }
            if(s->connection.zmodem)timeout=1;
            if(session_ready(s) && c->queued<=OUT_CAP/2 && c->used) timeout=0;
            if(s->core.wake && s->core.wake>now && s->core.wake-now<(uint64_t)timeout) timeout=(int)(s->core.wake-now);
            fds[count].fd=c->fd;
            if(!s->core.closing && c->used<CAP && c->queued<=OUT_CAP/2) fds[count].events=POLLIN;
            if(c->ssh) {
                fds[count].events=POLLIN;
                if(ln_ssh_wants_write(c->ssh))fds[count].events|=POLLOUT;
            } else if(c->queued) fds[count].events|=POLLOUT;
            map[count]=i; count++;
        }
        int ready=poll_sockets(fds,count,timeout);
        if(ready<0) { if(retryable()) continue; result=1; break; }
        for(unsigned j=1;j<count;j++) {
            if(map[j]<0)continue;
            Connection *c=&connections[map[j]]; short ev=fds[j].revents;
            if(ev&(POLLERR|POLLNVAL)) { drop(c); continue; }
            if(c->ssh) { if(ev&POLLHUP)drop(c);continue; }
            if(ev&POLLOUT) {
                int n=(int)send(c->fd,(const char *)c->output,(int)c->queued,0);
                if(n>0) { c->tx_bytes+=(uint64_t)n;memmove(c->output,c->output+n,c->queued-(size_t)n); c->queued-=(size_t)n; }
                else if(n==0 || !retryable()) { drop(c); continue; }
            }
            if(ev&POLLIN) {
                size_t room=CAP-c->used; if(room>512) room=512;
                int n=(int)recv(c->fd,(char *)c->input+c->used,(int)room,0);
                if(n>0) { c->rx_bytes+=(uint64_t)n;c->used+=(size_t)n; }
                else if(n==0 || !retryable()) { drop(c); continue; }
            }
            if(ev&POLLHUP) drop(c);
        }
        for(int incoming=0;incoming<(ssh_server?2:1);incoming++) {
        if(fds[incoming].revents&POLLIN) {
            struct sockaddr_in peer; socklen_t peer_size=sizeof(peer);
            socket_t fd=accept(incoming?(socket_t)ln_ssh_listener(ssh_server):listener,(struct sockaddr *)&peer,&peer_size);
            if(fd!=INVALID) {
                int slot=-1;
                for(int i=0;i<SLOTS;i++) if(connections[i].fd==INVALID) { slot=i; break; }
                if(slot<0 || online(NULL)>=ln_limit("CONNECTIONS",CONNECTION_MAX) || shutdown_state.drain || next_id>CONTENT_ID_MAX || nonblock(fd)) close_socket(fd);
                else {
                    /* Interactive commands and connector receipts must not wait
                     * for a delayed ACK on a preceding short response. */
                    int no_delay=1;
                    if(setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,(const char *)&no_delay,sizeof(no_delay))) { close_socket(fd);continue; }
                    Connection *c=&connections[slot]; c->fd=fd;char address[48];if(inet_ntop(AF_INET,&peer.sin_addr,address,sizeof(address)))snprintf(c->peer,sizeof(c->peer),"%s:%u",address,(unsigned)ntohs(peer.sin_port));
                    if(incoming) { c->ssh=ln_ssh_accept(ssh_server,(int)fd,now_ms(),ssh_authenticate,c);if(!c->ssh) { drop(c);continue; } }
                    if(!terminal_init(&c->terminal,incoming?TERMINAL_PLAIN:terminal_mode,queue_bytes,c)) { drop(c); continue; }
                    SessionIO io={c,app_write,online,app_drained};
                    (void)session_init_ui(&c->app,io,next_id++,slot,board_enabled,member_mode,menu_mode,default_language,&ui_config);
                    if(!c->app.core.active) { drop(c);continue; }
                    if(!userlog_connect(slot,incoming?"RAW":terminal_mode==TERMINAL_TELNET?"TELNET":"RAW",now_ms())) { fprintf(stderr,"userlog: connection write failed\n");drop(c);result=1;stopping=1;break; }
                    c->userlog_registered=1;c->clock.started=c->clock.last_input=now_ms();
                    c->app.connection.zmodem_available=zhelper!=NULL;
                    if(shutdown_state.at) { uint64_t clock=now_ms();unsigned remaining=connection_shutdown_remaining(&shutdown_state,clock);session_notify(&c->app,MSG_SHUTDOWN_NOTICE,remaining,shutdown_state.reason); }

                    vault_worker_session(slot,c->app.core.id,1);
                }
            }
        }
        } /* both listeners */
    }
    for(int i=0;i<SLOTS;i++) drop(&connections[i]);
    #ifndef _WIN32
    for(int i=0;i<25 && (zhost_pending() || doors_pending());i++) { zhost_reap(now_ms());doors_reap();struct timespec pause={0,100000000};nanosleep(&pause,NULL); }
#endif
    ln_ssh_server_free(ssh_server);close_socket(listener); userlog_close();board_stop(); vault_bridge_close();chat_log_close();
#ifdef _WIN32
    WSACleanup();
#endif
    return result || log_failed;
}
