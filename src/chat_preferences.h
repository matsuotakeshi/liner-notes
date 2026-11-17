#ifndef CHAT_PREFERENCES_H
#define CHAT_PREFERENCES_H
#include <stdint.h>
/* Transport-independent, immutable snapshot from the identity/settings owner. */
typedef struct { uint32_t revision; int deny; char muted[32][33]; } ChatPreferences;
typedef enum { CHAT_PREF_QUERY, CHAT_PREF_RECEIVE_ON, CHAT_PREF_RECEIVE_OFF,
               CHAT_PREF_MUTE, CHAT_PREF_UNMUTE } ChatPreferenceAction;
#endif
