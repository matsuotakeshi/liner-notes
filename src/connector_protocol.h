#ifndef LINER_CONNECTOR_PROTOCOL_H
#define LINER_CONNECTOR_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
/* Pure wire adapters: recognizing a token does NOT authorize an operation. */
typedef enum { CONNECTOR_NONE, CONNECTOR_OPEN, CONNECTOR_SYNC,
    CONNECTOR_LITERAL, CONNECTOR_BLOCK, CONNECTOR_LINKBLOCK,
    CONNECTOR_DATA, CONNECTOR_TRANSFER, CONNECTOR_VIEW, CONNECTOR_UPDATE } ConnectorEnvelopeKind;
typedef struct {
    ConnectorEnvelopeKind kind;
    const char *payload;size_t size; /* borrows input until it is modified */
} ConnectorEnvelope;
/* Input contains size readable bytes. token is NULL/empty or a NUL-terminated
 * 64-byte negotiated marker. Payloads are not necessarily NUL-terminated. */
ConnectorEnvelope connector_envelope(const char *,size_t,const char *);

typedef struct {
    char action;uint32_t cursor,parent;uint64_t timestamp;
    char origin[65],article[65],author[33],title[81];
} LinkWireRequest;
int link_wire_command(const char *);
/* Mutable, NUL-terminated request. Body uses caller-owned storage (no large
 * stack duplicate). On failure output is empty and request is zeroed.
 * No signature, membership, ACL or storage decisions are made here. */
int link_wire_parse(char *,LinkWireRequest *,char *,size_t);

typedef struct {
    unsigned long connection;
    unsigned user,board,article,level,editor_kind,mail_current,confirmation,input_limit;
    const char *username; /* validated BBS login name; borrowed for format call */
    int admin,sigop,editing,secret,password_mode,mail,chat,talk,pager,gather,
        input,binary,door,members,feature_mail,feature_chat,feature_vault;
} ConnectorState;
/* Formats protocol 1 with optional capability fields. Never returns a partial response. */
int connector_state_format(const ConnectorState *,const char *,char *,size_t);
#endif
