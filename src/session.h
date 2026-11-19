#ifndef SESSION_H
#define SESSION_H
#include "notification_core.h"
#include "connector_core.h"
#include "batch_core.h"
#include "traversal_core.h"
#include "preference_core.h"
#include "mail_navigation_core.h"
#include "list_navigation_core.h"
#include "message_flow_core.h"
#include "form_flow_core.h"
#include <stddef.h>
#include <stdint.h>
#include "board.h"
#include "ui.h"
#include "session_core.h"
#include "session_state.h"
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
    int connector_literal;
    ConnectorBlock connector_block;
} SessionConnection;
typedef struct {
    SessionCore core;
    SessionAuth auth;
    SessionPresentation presentation;
    SessionEditor editor;
    SessionReading reading;
    SessionFiles files;
    SessionMailState mail_state;
    SessionConversation conversation;
    SessionConnection connection;
} Session;
void session_init(Session *,SessionIO,unsigned long id,int slot,int boards,int members,int menu,Language);
void session_dimensions(Session *,unsigned columns,unsigned rows);
void session_abort(Session *);
void session_door_finish(Session *);
void session_scan_stop(Session *);
void session_scan_pause(Session *);
/* Invalidate board listings without changing pending/automatic reader state. */
void session_board_refresh(Session *);
/* Invalidate only access-log paging, keeping outstanding worker state. */
void session_access_refresh(Session *);
int session_auto_read(const Session *);
void session_chat_kick(Session *);
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
void session_announce(Session *,const char *);
void session_tick(Session *,uint64_t now);
/* Runtime validates session/slot/transfer generation before delivering receipts. */
void session_transfer_receipt(Session *,const char *);
void session_zmodem_finish(Session *,int success,unsigned id,uint64_t size,int recovery);
void session_mail_arrived(Session *,uint32_t recipient);
void session_mail_notify(Session *,uint32_t user,int deny,uint32_t revision);
int session_binary_byte(Session *,unsigned char,uint64_t now);
void session_result(Session *,const BoardResult *,uint64_t now);
#endif
