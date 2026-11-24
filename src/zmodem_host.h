#ifndef ZMODEM_HOST_H
#define ZMODEM_HOST_H
#include <stddef.h>
#include <stdint.h>
enum { ZHOST_RECEIVE_PERSONAL = 13, ZHOST_PERSONAL_MAX = 10240 };
typedef struct ZHost ZHost;
/* Separate byte channel and restricted authenticated control channel. */
int zhost_pending(void);
int zhost_supported(const char *helper);
ZHost *zhost_start(const char *helper,int receive,unsigned id,const char *user,const char *text,unsigned port,uint64_t now,unsigned protocol);
int zhost_room(ZHost *);
int zhost_input(ZHost *,unsigned char);
int zhost_pump(ZHost *,int (*write)(void *,const unsigned char *,size_t),void *,uint64_t now);
const char *zhost_request(ZHost *);
void zhost_submitted(ZHost *);
int zhost_answer(ZHost *,const char *);
void zhost_receipt(ZHost *,unsigned *id,uint64_t *size,int *recovery);
const char *zhost_personal_payload(ZHost *,size_t *size);
int zhost_progress(ZHost *,uint64_t *,uint64_t *);
void zhost_stop(ZHost *);
void zhost_reap(uint64_t now);
#endif
