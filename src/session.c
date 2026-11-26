#define _POSIX_C_SOURCE 200809L
#include "session.h"
#include "session_io.h"
#include "terminal_pager.h"
#include "runtime_limits.h"
#include <string.h>
#include <openssl/crypto.h>

void session_abort(Session *s) { session_core_abort(&s->core); }

void session_dimensions(Session *s,unsigned columns,unsigned rows) {
    TerminalDimensions size=terminal_dimensions(columns,rows);s->connection.columns=size.columns;s->connection.rows=size.rows;
}

static int enqueue_operation(void *context,const BoardRequest *q) { (void)context;return board_submit(q); }
SessionOperationStatus session_submit(Session *s,OperationRequest *q) {
    return session_core_submit(&s->core,q,(SessionOperationPort){NULL,enqueue_operation});
}

int session_init_ui(Session *s,SessionIO io,unsigned long id,int slot,int boards,int members,int menu,Language language,const SessionUiConfig *config) {
    SessionUiConfig defaults=session_ui_default();
    if(!config)config=&defaults;
    memset(s,0,sizeof(*s));
    if(!session_ui_config_valid(config) || !io.write)return 0;
    s->ui_binding.config=*config;s->ui_binding.active=config->adapters[config->default_index];
    s->connection.io=io;session_core_init(&s->core,id,slot,boards,members);
    s->core.login_required=s->ui_binding.active->login_required;
    s->connection.binary_allowed=1;session_dimensions(s,80,24);
    if(!session_ui_allocate(s,config->default_index)) { session_abort(s);return 0; }
    s->ui_binding.active->enter(s,1,menu,language);
    return s->core.active;
}

void session_init(Session *s,SessionIO io,unsigned long id,int slot,int boards,int members,int menu,Language language) {
    (void)session_init_ui(s,io,id,slot,boards,members,menu,language,NULL);
}

void session_dispose(Session *s) { connector_block_clear(&s->connection.connector_block);session_core_abort(&s->core);session_ui_dispose(s);OPENSSL_cleanse(s,sizeof(*s)); }

int session_ready(const Session *s) { return s->core.active && !s->core.wake && !s->core.pending && !s->core.closing && !s->connection.zmodem; }

int session_secret(const Session *s) { return session_ui_snapshot(s).secret; }


int session_line(Session *s,const char *text,size_t size,uint64_t now) {
    if(!session_ready(s)) return 0;
    if(size>ln_limit("INPUT",INPUT_LINE_MAX) || memchr(text,0,size)) { session_abort(s); return 0; }
    char line[INPUT_LINE_MAX+1]; memcpy(line,text,size); line[size]=0; s->core.now=now;
    if(size==3 && !memcmp(line,"RAW",3) && session_ui_legacy_connector(s)) {
        OPENSSL_cleanse(line,sizeof(line));return 1;
    }
    if(s->ui_binding.active->connector_line(s,line,size)) { OPENSSL_cleanse(line,sizeof(line));return 1; }
    s->ui_binding.active->line(s,line,size); OPENSSL_cleanse(line,sizeof(line)); return 1;
}

int session_keys(const Session *s) {
    const SessionUiAdapter *a=s->ui_binding.active;
    return a && a->keys && a->key && !session_secret(s) && a->keys(s);
}

void session_key(Session *s,unsigned char key,uint64_t now) {
    if(!session_ready(s) || !session_keys(s))return;
    s->core.now=now;
    s->ui_binding.active->key(s,key);
}

void session_revoke(Session *s) {
    int notify=s->core.active && !s->core.closing;
    int transport=s->connection.door || s->connection.binary || s->connection.zmodem;
    session_core_revoke(&s->core);
    board_conference_session(s->core.slot,0,0,0);
    session_ui_discard_inactive(s);
    if(s->ui_binding.active && s->ui_binding.state)s->ui_binding.active->cleanup(s,1,notify && !transport);
    if(transport)session_abort(s);
}
int session_logout(Session *s) {
    if(!session_core_logout(&s->core))return 0;
    check_traversal_reset(&s->connection.connector_check);
    board_conference_session(s->core.slot,0,0,0);
    session_ui_discard_inactive(s);s->ui_binding.preferred[0]=0;s->ui_binding.explicit_choice=0;s->ui_binding.restore_pending=0;return 1;
}

void session_impl_binary_frame(Session *s,unsigned char kind,const void *data,size_t size) {
    unsigned char frame[BOARD_REPLY+6];size_t written;
    if(!connector_frame_encode(kind,data,size,BOARD_REPLY,frame,sizeof(frame),&written)) { session_abort(s);return; }
    if(!s->connection.io.write(s->connection.io.context,(char *)frame,written))session_abort(s);
}

void session_impl_queue_text(Session *s,const char *text) {
    if(s->core.active && !s->connection.io.write(s->connection.io.context,text,strlen(text))) session_abort(s);
}
