/* Liner notes: a native C BBS inspired by RTBBS. No legacy ASM/ABI emulation. */
#define _POSIX_C_SOURCE 200809L
#include "runtime_limits.h"
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
#include "doors.h"
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
    int userlog_registered,idle_warned,time_warning,time_grace;uint64_t last_input,idle_close,started;
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
static uint64_t shutdown_at,shutdown_drain;static char shutdown_reason[513];
static volatile sig_atomic_t stopping;
static int log_failed;
static uint32_t idle_seconds=600,time_limits[6],time_warning_seconds=60,time_grace_seconds=30;
static void stop(int sig) { (void)sig; stopping=1; }
static void drop(Connection *c) {
    if(c->fd==INVALID)return; /* Unused slots contain no live secrets/resources. */
    if(c->app.user && c->app.exit_after_save!=3 && c->app.saved_ui!=session_ui_signature(&c->app)) {
        BoardRequest *q=calloc(1,sizeof(*q));
        if(q) { q->op='V';q->number=1;q->ui_aux=(unsigned)c->app.editor_numbers | (c->app.transfer_protocol<<1);q->user=c->app.user;q->user_epoch=c->app.user_epoch;q->cursor=c->app.ui.language;q->title[0]=c->app.pager_enabled?'1':'0';q->scope=c->app.editor_line;q->audit_article=c->app.columns;q->audit_actor=c->app.rows;
            if(!board_save_disconnect(q)) { fprintf(stderr,"disconnect preferences: queue failed\n");log_failed=1; }free(q);
        } else { fprintf(stderr,"disconnect preferences: allocation failed\n");log_failed=1; }
    }
    if(c->fd!=INVALID && c->userlog_registered && (!userlog_update(c->app.slot,c->app.user,now_ms()) || !userlog_disconnect(c->app.slot,now_ms()))) { fprintf(stderr,"userlog: disconnect write failed\n");log_failed=1;stopping=1; }
    door_stop(c->door);c->door=NULL;
    zhost_stop(c->zhost);c->zhost=NULL;
    vault_worker_session(c->app.slot,c->app.id,0);
    if(c->fd!=INVALID) close_socket(c->fd);
    session_dispose(&c->app); terminal_dispose(&c->terminal);
    /* Only output bytes ever written can contain data. Avoid touching several
     * MiB of unused BSS for each idle connection while shutting down. */
    OPENSSL_cleanse(c->input,sizeof(c->input));OPENSSL_cleanse(c->output,c->output_highwater);
    OPENSSL_cleanse(c,offsetof(Connection,app));
    OPENSSL_cleanse((char *)c+offsetof(Connection,used),sizeof(*c)-offsetof(Connection,used));c->fd=INVALID;
}
static int app_drained(void *context) { return ((Connection *)context)->queued==0; }
static int queue_bytes(void *context,const unsigned char *data,size_t size) {
    Connection *c=context;
    if(size>OUT_CAP-c->queued) return 0;
    memcpy(c->output+c->queued,data,size); c->queued+=size;if(c->queued>c->output_highwater)c->output_highwater=c->queued;return 1;
}
static int app_write(void *context,const char *data,size_t size) {
    Connection *c=context;
    terminal_set_binary(&c->terminal,c->app.binary || c->app.zmodem || c->app.door);
    if(!terminal_write(&c->terminal,(const unsigned char *)data,size))return 0;
    /* Reprint a partially typed chat line after an asynchronous prompt. The
     * editor buffer is unchanged; never echo a password or binary payload. */
    if(c->app.chat_notice && c->app.ui.enabled && c->terminal.size &&
       c->terminal.mode!=TERMINAL_RAW && c->app.chat_prompting)
        return terminal_write(&c->terminal,(unsigned char *)c->terminal.line,c->terminal.size);
    return 1;
}
static unsigned online(void *context) {
    (void)context; unsigned n=0;
    for(int i=0;i<SLOTS;i++) if(connections[i].fd!=INVALID) n++;
    return n;
}

static void notify_all(MessageId id,uint32_t number,const char *body) {
    for(int i=0;i<SLOTS;i++)if(connections[i].fd!=INVALID) {
        Session *s=&connections[i].app;char text[900];
        if(id==MSG_SHUTDOWN_NOTICE)snprintf(text,sizeof(text),ui_text(&s->ui,id),number,body);
        else if(id==MSG_ADMIN_NOTICE)snprintf(text,sizeof(text),ui_text(&s->ui,id),body);
        else snprintf(text,sizeof(text),"%s",ui_text(&s->ui,id));
        session_announce(s,text);
    }
}
static void service_result(BoardResult *r) {
    if(r->service_action=='J') {
        if(!r->service_number) { doors_list(r->service_next,r->text,sizeof(r->text));return; }
        if(r->slot<0 || r->slot>=SLOTS) return;
        Connection *c=&connections[r->slot];Session *s=&c->app;
        if(c->fd==INVALID || s->id!=r->session || !s->user || s->closing || !s->binary_allowed || c->door) { strcpy(r->text,"ERR game unavailable\r\n");return; }
        c->door=door_start(r->service_number,r->service_next,s->user,s->username,s->columns,s->rows,now_ms());
        if(!c->door) { strcpy(r->text,errno==EBUSY?"ERR game busy\r\n":"ERR game unavailable\r\n");return; }
        s->door=1;c->used=0;terminal_clear(&c->terminal);s->scanning=s->auto_read=0;
        snprintf(r->text,sizeof(r->text),s->ui.language==LANGUAGE_JA?"ゲーム開始。Ctrl-] でBBSへ戻ります。\r\n":"Game started. Ctrl-] returns to BBS.\r\n");return;
    }
    if(r->all_accounts_changed)for(int i=0;i<SLOTS;i++)if(connections[i].fd!=INVALID && connections[i].app.id!=r->session)session_revoke(&connections[i].app);
    if(r->access_refresh)for(int i=0;i<SLOTS;i++)if(connections[i].fd!=INVALID) {
        Session *s=&connections[i].app;
        if(!strncmp(s->report,"BOARDACCESS ",12)) { s->report[0]=0;s->report_next=0;s->page[0]=0;s->page_offset=0; }
    }
    if((!r->service_action || strchr("GgM",r->service_action)) && !r->board_refresh)return;
    if(r->service_action=='u') { userlog_table(r->service_number,r->service_next,!strcmp(r->service_text,"ALL"),now_ms(),r->text,sizeof(r->text));return; }
    if(r->service_action=='U' || r->service_action=='V') {
        userlog_report(r->service_number,r->service_next,!strcmp(r->service_text,"ALL"),r->service_action=='V',now_ms(),r->text,sizeof(r->text));return;
    }
    if(r->service_action=='b' || r->board_refresh) {
        for(int i=0;i<SLOTS;i++)if(connections[i].fd!=INVALID) {
            Session *s=&connections[i].app;s->page[0]=0;s->page_offset=0;s->scanning=0;s->ui.next=0;s->ui.view=UI_HOME;s->ui.board_title[0]=0;s->report[0]=0;s->report_next=0;
        }
        return;
    }
    if(r->service_action=='L') {
        size_t used=(size_t)snprintf(r->text,sizeof(r->text),"CONNECTIONS\r\n");
        for(int i=0;i<SLOTS;i++) { Connection *c=&connections[i];if(c->fd==INVALID)continue;
            used+=(size_t)snprintf(r->text+used,sizeof(r->text)-used,"CONNECTION %lu %s %s %s %s %llu %llu %llu %llu %zu %u %u\r\n",c->app.id,c->app.user?c->app.username:"guest",c->app.door?"DOOR":c->app.zmodem?"ZMODEM":c->app.binary?"BINARY":session_secret(&c->app)?"AUTH":c->app.chatting?"CHAT":c->app.editing?"EDITOR":"BBS",c->terminal.mode==TERMINAL_TELNET?"TELNET":"RAW",*c->peer?c->peer:"unknown",(unsigned long long)((now_ms()-c->started)/1000),(unsigned long long)((now_ms()-c->last_input)/1000),(unsigned long long)c->rx_bytes,(unsigned long long)c->tx_bytes,c->queued,c->app.columns,c->app.rows);
        }
        snprintf(r->text+used,sizeof(r->text)-used,"END CONNECTIONS\r\n");
    } else if(r->service_action=='A') { notify_all(MSG_ADMIN_NOTICE,0,r->service_text);strcpy(r->text,"ANNOUNCE SENT\r\n"); }
    else if(r->service_action=='K') {
        int found=0;for(int i=0;i<SLOTS;i++)if(connections[i].fd!=INVALID && connections[i].app.id==r->service_number) { drop(&connections[i]);found=1;break; }
        snprintf(r->text,sizeof(r->text),found?"DISCONNECTED %u\r\n":"ERR no connection\r\n",r->service_number);
    } else if(r->service_action=='C') { shutdown_at=shutdown_drain=0;shutdown_reason[0]=0;notify_all(MSG_SHUTDOWN_CANCELLED,0,"");strcpy(r->text,"SHUTDOWN CANCELLED\r\n"); }
    else if(r->service_action=='S') {
        shutdown_at=now_ms()+(uint64_t)r->service_number*1000;shutdown_drain=0;snprintf(shutdown_reason,sizeof(shutdown_reason),"%s",r->service_text);
        notify_all(MSG_SHUTDOWN_NOTICE,r->service_number,shutdown_reason);snprintf(r->text,sizeof(r->text),"SHUTDOWN SCHEDULED %u\r\n",r->service_number);
    }
}
static int zbytes(void *context,const unsigned char *bytes,size_t size) {
    Connection *c=context;return terminal_write(&c->terminal,bytes,size);
}
static void zadvance(Connection *c,uint64_t now) {
    Session *s=&c->app;
    if(!s->active || s->closing) { drop(c);return; }
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
        c->zhost=zhost_start(zhelper,s->zmodem_receive,s->zmodem_id,s->username,s->zmodem_text,service_port,now,s->zmodem_protocol);
        if(!c->zhost) { c->zresult=0;c->zid=c->zsize=0;c->zrecovery=0;c->zend=c->zquiet=now;return; }
    }
    if(c->queued<=OUT_CAP/2) {
        int done=zhost_pump(c->zhost,zbytes,c,now);
        uint64_t bytes,total;
        if(zhost_progress(c->zhost,&bytes,&total)) {
            char text[192];unsigned percent=total?(unsigned)(bytes*100/total):100;
            snprintf(text,sizeof(text),s->ui.language==LANGUAGE_JA?"\r\n受信データを登録中: %u%% (%llu / %llu bytes)\r\n":"\r\nRegistering received data: %u%% (%llu / %llu bytes)\r\n",percent,(unsigned long long)bytes,(unsigned long long)total);
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
        BoardRequest q={0};q.slot=s->slot;q.session=s->id;q.user=s->user;q.user_epoch=s->user_epoch;q.zcontrol=c->zgeneration;memcpy(q.vault_grants,s->vault_grants,sizeof(q.vault_grants));q.scope=(int)s->zmodem_protocol+1;
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
    if(!s->active || s->closing) { drop(c);return; }
    terminal_set_binary(&c->terminal,1);int done=0;size_t used=0;
    unsigned old_width=c->terminal.telnet.width,old_height=c->terminal.telnet.height;
    while(used<c->used && door_room(c->door)) {
        unsigned char byte;int rc=terminal_binary_receive(&c->terminal,c->input[used++],&byte);
        if(rc<0) { drop(c);return; }if(!rc)continue;c->last_input=now;c->idle_warned=0;
        if(byte==29) { done=1;break; }if(!door_input(c->door,byte)) { done=-1;break; }
    }
    memmove(c->input,c->input+used,c->used-used);c->used-=used;
    if(c->terminal.mode==TERMINAL_TELNET &&
       (old_width!=c->terminal.telnet.width || old_height!=c->terminal.telnet.height)) {
        session_dimensions(s,c->terminal.telnet.width,c->terminal.telnet.height);
        /* A vanished PTY must not kill the BBS session: door_pump handles exit. */
        if(!done)(void)door_resize(c->door,s->columns,s->rows);
    }
    if(!done && c->queued<=OUT_CAP/2)done=door_pump(c->door,zbytes,c,now);
    if(done) {
        door_stop(c->door);c->door=NULL;s->door=0;c->used=0;terminal_clear(&c->terminal);terminal_set_binary(&c->terminal,0);
        session_door_finish(s);
    }
}
static void advance(Connection *c,uint64_t now) {
    Session *s=&c->app;
    if(s->door) { door_advance(c,now);return; }
    if(s->zmodem) { c->last_input=now;c->idle_warned=0;zadvance(c,now);return; }
    if(s->auto_read && c->used) {
        size_t consumed=0;int stop=0;
        while(consumed<c->used) {
            int rc=terminal_receive(&c->terminal,c->input[consumed++],0);
            if(rc<0) { drop(c);return; }
            if(rc>0 || c->terminal.size) { stop=1;break; }
        }
        memmove(c->input,c->input+consumed,c->used-consumed);c->used-=consumed;
        if(stop) { c->last_input=now;c->idle_warned=0;terminal_clear(&c->terminal);c->used=0;session_scan_stop(s); }
    }
    if(!s->auto_read || c->queued<=OUT_CAP/2)session_tick(s,now);
    if(!s->active) { drop(c); return; }
    if(!session_ready(s) || c->queued>OUT_CAP/2) return;
    size_t consumed=0;
    terminal_set_binary(&c->terminal,s->binary);
    while(consumed<c->used) {
        if(s->binary) {
            unsigned char byte;int decoded=terminal_binary_receive(&c->terminal,c->input[consumed++],&byte);
            if(decoded<0) { drop(c);return; }
            int complete=decoded?session_binary_byte(s,byte,now):0;
            if(complete<0 || !s->active) { drop(c);return; }
            if(complete)break;
            continue;
        }
        unsigned old_width=c->terminal.telnet.width,old_height=c->terminal.telnet.height;
        size_t old_size=c->terminal.size;
        c->terminal.uppercase=session_uppercase(s,c->terminal.line,c->terminal.size);
        int result=terminal_receive(&c->terminal,c->input[consumed++],session_secret(s));
        if(result>0 || c->terminal.size!=old_size) { c->last_input=now;c->idle_warned=0; }
        if(c->terminal.mode==TERMINAL_TELNET && (old_width!=c->terminal.telnet.width || old_height!=c->terminal.telnet.height))
            session_dimensions(s,c->terminal.telnet.width,c->terminal.telnet.height);
        if(result<0) { drop(c); return; }
        if(session_secret(s) && c->terminal.mode==TERMINAL_TELNET && !telnet_echo(&c->terminal.telnet)) { drop(c); return; }
        if(result>0) {
            s->binary_allowed=terminal_binary_ready(&c->terminal);
            session_line(s,c->terminal.line,c->terminal.size,now);
            terminal_clear(&c->terminal);
            /* Do not accept passwords from Telnet clients declining remote echo. */
            if(session_secret(s) && c->terminal.mode==TERMINAL_TELNET && !telnet_echo(&c->terminal.telnet)) session_abort(s);
            break;
        }
    }
    memmove(c->input,c->input+consumed,c->used-consumed); c->used-=consumed;
    OPENSSL_cleanse(c->input+c->used,CAP-c->used);
    if(!s->active) drop(c);
}
int main(int argc,char **argv) {
    const char *program=argv[0];
    const char *doors_path=NULL;
    const char *help_directory="help";int help_required=0;
    const char *user_log=NULL;
    const char *chat_log=NULL;char chat_default[4096];
    int structured_database=0;const char *limits_path=NULL,*database_path=NULL;
    const char *timezone="UTC",*site_info="sysinfo.txt",*site_bye="goodbye.txt";
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
        else if(!strcmp(argv[1],"--database-structured") && argc>2) { structured_database=1;database_path=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--database") && argc>2) { database_path=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--limits") && argc>2) { limits_path=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--sysinfo") && argc>2) { site_info=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--goodbye") && argc>2) { site_bye=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--timezone") && argc>2) { timezone=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--doors") && argc>2) { doors_path=argv[2];argc-=2;argv+=2; }
        else if(!strcmp(argv[1],"--gather-count") && argc>2) {
            char *end;unsigned long n=strtoul(argv[2],&end,10);if(*argv[2]<'0' || *argv[2]>'9' || *end || n>1000) { fprintf(stderr,"gather-count: 0..1000 required\n");return 2; }gather_limit=(unsigned)n;argc-=2;argv+=2;
        }
        else if(!strcmp(argv[1],"--rtbbs-member-policy")) { members_rtbbs_policy=1;argc--;argv++; }
        else if(!strcmp(argv[1],"--userlog-public")) { userlog_public=1;argc--;argv++; }
        else if((!strcmp(argv[1],"--userlog-keep") || !strcmp(argv[1],"--time-warning") || !strcmp(argv[1],"--time-grace")) && argc>2) {
            char *end;unsigned long n=strtoul(argv[2],&end,10);if(*argv[2]<'0' || *argv[2]>'9' || *end || n>1000000)return 2;
            if(!strcmp(argv[1],"--userlog-keep"))userlog_keep=(unsigned)n;else if(!strcmp(argv[1],"--time-warning"))time_warning_seconds=(unsigned)n;else time_grace_seconds=(unsigned)n;argc-=2;argv+=2;
        }
        else if(!strcmp(argv[1],"--level-seconds") && argc>2) {
            unsigned level,seconds;int n=0;if(sscanf(argv[2],"%u:%u%n",&level,&seconds,&n)!=2 || argv[2][n] || level>5 || seconds>86400)return 2;time_limits[level]=seconds;argc-=2;argv+=2;
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
            idle_seconds=(uint32_t)n;argc-=2;argv+=2;
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
        else if(!strcmp(argv[1],"--menu")) { menu_mode=1; argc--; argv++; }
        else if(!strcmp(argv[1],"--telnet")) { terminal_mode=TERMINAL_TELNET; argc--; argv++; }
        else if(!strcmp(argv[1],"--lang") && argc>2 && language_parse(argv[2],&default_language)) { argc-=2; argv+=2; }
        else { fprintf(stderr,"invalid option; use --zmodem-helper FILE, --vault-dir DIR, --bind IPv4, --menu-dir DIR, --menu, --telnet, --lang ja|en and/or --admins FILE\n"); return 2; }
    }
    char *end=NULL;
    if(doors_path && !door_lock_init(program)) { fprintf(stderr,"Door lock helper missing or unsafe\n");return 2; }
    if(!doors_config(doors_path)) { fprintf(stderr,"invalid doors configuration\n");return 2; }
    long port=(argc>=2 && argc<=5) ? strtol(argv[1],&end,10) : -1;
    if(port<0 || port>65535 || !end || end==argv[1] || *end) {
        fprintf(stderr,"usage: runtime [--database FILE | --database-structured FILE] [--level-seconds LEVEL:SECONDS] [--time-warning N] [--time-grace N] [--userlog-public] [--userlog-keep N] [--idle-seconds N] [--user-log FILE|-] [--gather-count N] [--chat-log FILE|-] [--max-articles N] [--max-mail N] [--max-user-posts N] [--banner FILE] [--disable chat|mail|vault] [--registration open|approval|closed] [--id-mode free|auto] [--id-prefix PREFIX] [--id-digits 1..8] [--id-first NAME] [--rtbbs-member-policy] [--login-text-dir DIR] [--registration-form simple|detailed] [--doors FILE] [--link-accounts FILE] [--zmodem-helper FILE] [--vault-dir DIR] [--bind IPv4] [--menu] [--menu-dir DIR] [--help-dir DIR] [--telnet] [--lang ja|en] [--admins FILE] PORT [BOARD_FILE [MEMBERS_FILE [CATALOG_FILE]]] (default bind: 127.0.0.1)\n"); return 2;
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
    int result=0;
    while(!stopping) {
        uint64_t clock=now_ms();
        if(shutdown_at && clock>=shutdown_at && !shutdown_drain) {
            shutdown_drain=clock+500;notify_all(MSG_SHUTDOWN_STOPPING,0,"");
            for(int i=0;i<SLOTS;i++)if(connections[i].fd!=INVALID) {
                Connection *c=&connections[i];if(c->app.zmodem || c->app.binary)drop(c);
                else { session_tick(&c->app,clock);c->app.closing=1; }
            }
        }
        if(shutdown_drain && clock>=shutdown_drain) { stopping=1;break; }
        zhost_reap(now_ms());
        BoardResult completion;
        board_completion_drain();
        while(board_poll(&completion)) {
            service_result(&completion);
            if(completion.zcontrol) {
                if(completion.slot>=0 && completion.slot<SLOTS) {
                    Connection *c=&connections[completion.slot];
                    if(c->fd!=INVALID && c->app.id==completion.session && c->zhost && c->zgeneration==completion.zcontrol) {
                        unsigned done_id=0;
                        if(c->app.zmodem_receive==2 && sscanf(completion.text,"DOWNLOADED %u",&done_id)==1)
                            for(unsigned b=0;b<c->app.batch_count;b++)if(c->app.batch[b]==done_id)c->app.batch_done[b]=1;
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
                if(c->fd!=INVALID && s->active && !s->closing && s->user==completion.chat_kick_user && (s->chatting || s->chat_pending)) {
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
                    Connection *c=&connections[i];if(c->fd!=INVALID && c->app.user==user && c->app.user_epoch<account_epochs[user]) {
                        c->used=0;c->queued=0;OPENSSL_cleanse(c->input,sizeof(c->input));terminal_clear(&c->terminal);session_revoke(&c->app);
                    }
                }
            }
            if(completion.account_user && completion.account_user<=MEMBER_MAX) {
                uint32_t user=completion.account_user;
                if(completion.password_changed && completion.slot>=0 && completion.slot<SLOTS) {
                    Session *owner=&connections[completion.slot].app;
                    if(owner->id==completion.session && owner->user==user && !owner->closing) owner->user_epoch=completion.account_epoch;
                }
                if(account_epochs[user]<completion.account_epoch) account_epochs[user]=completion.account_epoch;
                for(int i=0;i<SLOTS;i++) {
                    Connection *c=&connections[i];
                    if(c->fd!=INVALID && c->app.user==user && c->app.user_epoch<account_epochs[user]) {
                        c->used=0; c->queued=0; OPENSSL_cleanse(c->input,sizeof(c->input));
                        terminal_clear(&c->terminal); session_revoke(&c->app);
                    }
                }
            }
            if(completion.slot>=0 && completion.slot<SLOTS) {
                Connection *c=&connections[completion.slot];
                if(c->app.id==completion.session && completion.auth>0 && completion.auth<=MEMBER_MAX && completion.user_epoch<account_epochs[completion.auth]) {
                    c->used=0; c->queued=0; OPENSSL_cleanse(c->input,sizeof(c->input));
                    terminal_clear(&c->terminal); session_revoke(&c->app);
                } else session_result(&c->app,&completion,now_ms());
            }
        }
        doors_reap();
        pollfd_t fds[SLOTS+2]; int map[SLOTS+2]; unsigned count=1;
        memset(fds,0,sizeof(fds));
        fds[0].fd=listener; fds[0].events=POLLIN;
        int notification=board_completion_fd();
        if(notification>=0) { fds[count].fd=notification;fds[count].events=POLLIN;map[count++]=-1; }
        uint64_t now=now_ms(); int timeout=50;
        for(int i=0;i<SLOTS;i++) {
            Connection *c=&connections[i]; if(c->fd==INVALID) continue;
            if(!userlog_update(i,c->app.user,now) || !userlog_maintain()) { fprintf(stderr,"userlog: write failed\n");result=1;stopping=1;break; }
            if(!c->app.ui.admin && !c->app.closing) {
                unsigned level=c->app.user && c->app.member_level<6?c->app.member_level:0;uint64_t limit=(uint64_t)time_limits[level]*1000,elapsed=now>=c->started?now-c->started:0;
                if(limit && elapsed>=limit+(uint64_t)time_grace_seconds*1000) { drop(c);continue; }
                if(limit && !c->app.door && !c->app.zmodem && !c->app.binary && !session_secret(&c->app)) {
                    const char *message=NULL;
                    if(elapsed>=limit && !c->time_grace) { c->time_grace=1;message=c->app.ui.language==LANGUAGE_JA?"接続時間の上限です。猶予時間後に切断します。保存して終了してください。\r\n":"Session limit reached. Save your work before the grace period ends.\r\n"; }
                    else if(!c->time_warning && elapsed+(uint64_t)time_warning_seconds*1000>=limit) { c->time_warning=1;message=c->app.ui.language==LANGUAGE_JA?"接続時間の上限が近づいています。\r\n":"Your session time limit is approaching.\r\n"; }
                    if(message && !app_write(c,message,strlen(message))) { drop(c);continue; }
                }
            }
            if(c->idle_close && now>=c->idle_close) { drop(c);continue; }
            if(c->app.binary || c->app.pending || c->app.zmodem) { c->last_input=now;c->idle_warned=0; }
            advance(c,now);
            if(c->fd!=INVALID && idle_seconds && !c->app.ui.admin && !c->app.closing && !c->app.pending && !c->app.binary && !c->app.zmodem) {
                uint64_t elapsed=now>=c->last_input?now-c->last_input:0,limit=(uint64_t)idle_seconds*1000;
                uint64_t warning=idle_seconds>120?60000:limit/2;
                if(elapsed>=limit) {
                    const char *msg=c->app.ui.enabled?ui_text(&c->app.ui,MSG_IDLE_CLOSED):"BYE idle timeout\r\n";
                    if(!app_write(c,msg,strlen(msg))) { drop(c);continue; }
                    c->app.closing=1;c->idle_close=now+1000;
                } else if(!c->idle_warned && elapsed>=limit-warning && c->app.ui.enabled && !session_secret(&c->app)) {
                    const char *msg=ui_text(&c->app.ui,MSG_IDLE_WARNING);if(!app_write(c,msg,strlen(msg))) { drop(c);continue; }c->idle_warned=1;
                }
            }
            if(c->fd!=INVALID && !userlog_update(i,c->app.user,now)) { result=1;stopping=1;break; }
            if(c->fd==INVALID) continue;
            Session *s=&c->app;
            if(s->closing && !c->queued) { drop(c); continue; }
            if(c->used==CAP && !s->zmodem) { drop(c); continue; }
            if(s->zmodem)timeout=1;
            if(session_ready(s) && c->queued<=OUT_CAP/2 && c->used) timeout=0;
            if(s->wake && s->wake>now && s->wake-now<(uint64_t)timeout) timeout=(int)(s->wake-now);
            fds[count].fd=c->fd;
            if(!s->closing && c->used<CAP && c->queued<=OUT_CAP/2) fds[count].events=POLLIN;
            if(c->queued) fds[count].events|=POLLOUT;
            map[count]=i; count++;
        }
        int ready=poll_sockets(fds,count,timeout);
        if(ready<0) { if(retryable()) continue; result=1; break; }
        for(unsigned j=1;j<count;j++) {
            if(map[j]<0)continue;
            Connection *c=&connections[map[j]]; short ev=fds[j].revents;
            if(ev&(POLLERR|POLLNVAL)) { drop(c); continue; }
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
        if(fds[0].revents&POLLIN) {
            struct sockaddr_in peer; socklen_t peer_size=sizeof(peer);
            socket_t fd=accept(listener,(struct sockaddr *)&peer,&peer_size);
            if(fd!=INVALID) {
                int slot=-1;
                for(int i=0;i<SLOTS;i++) if(connections[i].fd==INVALID) { slot=i; break; }
                if(slot<0 || online(NULL)>=ln_limit("CONNECTIONS",CONNECTION_MAX) || shutdown_drain || next_id>CONTENT_ID_MAX || nonblock(fd)) close_socket(fd);
                else {
                    /* Interactive commands and connector receipts must not wait
                     * for a delayed ACK on a preceding short response. */
                    int no_delay=1;
                    if(setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,(const char *)&no_delay,sizeof(no_delay))) { close_socket(fd);continue; }
                    Connection *c=&connections[slot]; c->fd=fd;char address[48];if(inet_ntop(AF_INET,&peer.sin_addr,address,sizeof(address)))snprintf(c->peer,sizeof(c->peer),"%s:%u",address,(unsigned)ntohs(peer.sin_port));
                    if(!terminal_init(&c->terminal,terminal_mode,queue_bytes,c)) { drop(c); continue; }
                    SessionIO io={c,app_write,online,app_drained};
                    session_init(&c->app,io,next_id++,slot,board_enabled,member_mode,menu_mode,default_language);
                    if(!userlog_connect(slot,terminal_mode==TERMINAL_TELNET?"TELNET":"RAW",now_ms())) { fprintf(stderr,"userlog: connection write failed\n");drop(c);result=1;stopping=1;break; }
                    c->userlog_registered=1;c->started=c->last_input=now_ms();
                    c->app.zmodem_available=zhelper!=NULL;
                    if(shutdown_at) { char text[900];uint64_t clock=now_ms();unsigned remaining=clock<shutdown_at?(unsigned)((shutdown_at-clock+999)/1000):0;snprintf(text,sizeof(text),ui_text(&c->app.ui,MSG_SHUTDOWN_NOTICE),remaining,shutdown_reason);session_announce(&c->app,text); }

                    vault_worker_session(slot,c->app.id,1);
                }
            }
        }
    }
    for(int i=0;i<SLOTS;i++) drop(&connections[i]);
    #ifndef _WIN32
    for(int i=0;i<25 && (zhost_pending() || doors_pending());i++) { zhost_reap(now_ms());doors_reap();struct timespec pause={0,100000000};nanosleep(&pause,NULL); }
#endif
    close_socket(listener); userlog_close();board_stop(); vault_bridge_close();chat_log_close();
#ifdef _WIN32
    WSACleanup();
#endif
    return result || log_failed;
}
