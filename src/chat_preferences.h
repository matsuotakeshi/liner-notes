#ifndef CHAT_PREFERENCES_H
#define CHAT_PREFERENCES_H
#include <stdint.h>
/* Transport-independent, immutable snapshot from the identity/settings owner. */
typedef struct { uint32_t revision; int deny; char muted[32][33]; } ChatPreferences;
#endif
