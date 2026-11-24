#ifndef SESSION_H
#define SESSION_H
#include "connector_core.h"
#include <stddef.h>
#include <stdint.h>
#include "board.h"
#include "session_core.h"
#include "session_ui.h"
/* The adapter owns queuing/backpressure. write copies bytes synchronously and
 * returns 0 on failure. No OS handles, Telnet commands or HTTP frames here. */
typedef struct {
    void *context;
    int (*write)(void *context,const char *text,size_t size);
    unsigned (*online)(void *context);
    /* Optional for buffered transports; NULL means synchronous delivery. */
    int (*drained)(void *context);
} SessionIO;
typedef struct {
    SessionIO io;
    unsigned columns,rows;
    int door;
    char upload_description[241];
    char upload_filename[101];
    int upload_batch;
    unsigned transfer_protocol;
    unsigned zmodem_protocol;
    int zmodem_available;
    int zmodem;
    int zmodem_start;
    int zmodem_receive;
    uint32_t zmodem_id;
    char zmodem_text[513];
    int binary;
    int binary_allowed;
    int binary_menu;
    ConnectorReader binary_reader;
    char connector_token[65];
    int legacy_connector;
    int connector_literal;
    ConnectorBlock connector_block;
} SessionConnection;
struct Session {
    SessionUiBinding ui_binding;
    SessionCore core;
    SessionConnection connection;
};
int session_init_ui(Session *,SessionIO,unsigned long id,int slot,int boards,int members,int menu,Language,const SessionUiConfig *);
void session_init(Session *,SessionIO,unsigned long id,int slot,int boards,int members,int menu,Language);
void session_dimensions(Session *,unsigned columns,unsigned rows);
void session_abort(Session *);
int session_logout(Session *);
SessionOperationStatus session_submit(Session *,OperationRequest *);
void session_notify(Session *,MessageId,uint32_t,const char *);
int session_notice(const Session *,SessionNotice,unsigned,uint64_t,uint64_t,char *,size_t);
void session_door_finish(Session *);
void session_scan_stop(Session *);
void session_scan_pause(Session *);
/* Invalidate board listings without changing pending/automatic reader state. */
void session_board_refresh(Session *);
/* Invalidate only access-log paging, keeping outstanding worker state. */
void session_access_refresh(Session *);
int session_auto_read(const Session *);
void session_chat_kick(Session *);
int session_chat_event(void *,const ChatEvent *);
void session_revoke(Session *);
uint64_t session_ui_signature(const Session *);
/* NULL queries eligibility only. Otherwise zero/fill a caller-owned request.
 * Runtime allocates/enqueues it; Member Core still authorizes the saved epoch. */
int session_disconnect_preferences(const Session *,BoardRequest *);
void session_dispose(Session *);
int session_ready(const Session *);
int session_secret(const Session *);
int session_uppercase(const Session *,const char *,size_t);
/* Caller retains a line while not ready; one accepted line per scheduler turn.
 * Secret input must not be echoed by adapters. Lines exclude CR/LF. */
int session_line(Session *,const char *text,size_t size,uint64_t now);
int session_keys(const Session *);
void session_key(Session *,unsigned char,uint64_t now);
void session_announce(Session *,const char *);
void session_tick(Session *,uint64_t now);
/* Runtime validates session/slot/transfer generation before delivering receipts. */
void session_transfer_receipt(Session *,const char *);
int session_personal_received(Session *,unsigned long session,uint32_t user,uint32_t epoch,const char *,size_t);
void session_zmodem_finish(Session *,int success,unsigned id,uint64_t size,int recovery);
void session_mail_arrived(Session *,uint32_t recipient);
void session_mail_notify(Session *,uint32_t user,int deny,uint32_t revision);
int session_binary_byte(Session *,unsigned char,uint64_t now);
void session_result(Session *,const BoardResult *,uint64_t now);
#endif
