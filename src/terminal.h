#ifndef TERMINAL_H
#define TERMINAL_H
#include "telnet.h"
#include "app_limits.h"
/* PLAIN is usable by a future serial/PTY adapter; it is not a COM driver. */
typedef enum { TERMINAL_RAW, TERMINAL_TELNET, TERMINAL_PLAIN } TerminalMode;
typedef struct {
    TerminalMode mode;
    Telnet telnet;
    ByteSink write;
    void *context;
    char line[INPUT_LINE_MAX+2]; /* INPUT_LINE_MAX data bytes, optional raw CR, and terminator */
    size_t size;
    int after_cr,escape,binary,uppercase;
    unsigned escape_size;
} Terminal;
int terminal_init(Terminal *,TerminalMode,ByteSink,void *);
int terminal_write(Terminal *,const unsigned char *,size_t);
/* Return 1 for a complete line, 0 for incomplete/control, -1 on failure.
 * Caller must consume the line and call terminal_clear before further input.
 * secret is sampled for EVERY byte, never once per received packet. */
int terminal_binary_ready(const Terminal *);
void terminal_set_binary(Terminal *,int);
/* Decoded binary byte; no echo, CR translation, ANSI editing or EC/EL. */
int terminal_binary_receive(Terminal *,unsigned char,unsigned char *);
int terminal_receive(Terminal *,unsigned char,int secret);
void terminal_clear(Terminal *);
void terminal_dispose(Terminal *);
#endif
