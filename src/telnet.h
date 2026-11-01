#ifndef TELNET_H
#define TELNET_H
#include <stddef.h>
typedef int (*ByteSink)(void *context,const unsigned char *data,size_t size);
enum { TN_NONE=0, TN_DATA=1, TN_ERASE=2, TN_KILL=3 };
typedef struct {
    unsigned char local[256],remote[256];
    int state,verb,option;
    unsigned char sub[4];
    size_t sub_size;
    unsigned width,height;
    ByteSink write;
    void *context;
} Telnet;
int telnet_init(Telnet *,ByteSink,void *);
/* One byte at a time, preserving state across all packet boundaries.
 * -1: protocol/output limit, 0: control only, positive: terminal event. */
int telnet_receive(Telnet *,unsigned char byte,unsigned char *data);
int telnet_write(Telnet *,const unsigned char *,size_t);
int telnet_echo(const Telnet *);
int telnet_binary(const Telnet *);
#endif
