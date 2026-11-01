#ifndef CONTENT_LIMITS_H
#define CONTENT_LIMITS_H
/* UTF-8 bytes, including CRLF; wire input remains bounded per line. */
#define CONTENT_BODY 262144
#define CONTENT_DISPLAY (CONTENT_BODY*8+8192)
#endif
