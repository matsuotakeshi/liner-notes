#ifndef SESSION_IO_H
#define SESSION_IO_H
#define LINER_VERSION_TEXT "Liner notes 0.76"
#include "session.h"
/* Authorized adapter output. Queue/write failure invalidates the Session.
 * No RTBBS state, interpretation of text, or permission grant occurs here. */
void session_impl_binary_frame(Session *,unsigned char,const void *,size_t);
void session_impl_queue_text(Session *,const char *);
#endif
