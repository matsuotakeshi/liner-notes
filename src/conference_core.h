#ifndef LINER_CONFERENCE_CORE_H
#define LINER_CONFERENCE_CORE_H
#include <stdint.h>
typedef struct sqlite3 sqlite3;

enum { CONFERENCE_ROOMS = 1500, CONFERENCE_PAGE = 8, CONFERENCE_TEXT = 512 };
#define CONFERENCE_LAST_ID 2147483646u
typedef enum { CONFERENCE_LIST, CONFERENCE_HISTORY, CONFERENCE_UNREAD, CONFERENCE_POST,
    CONFERENCE_POSITION, CONFERENCE_SEEN, CONFERENCE_CREATE, TOPIC_HISTORY, TOPIC_UNREAD,
    TOPIC_REVERSE, TOPIC_POST, TOPIC_DELETE, TOPIC_SEEN } ConferenceAction;
typedef enum { CONFERENCE_OK, CONFERENCE_LOGIN, CONFERENCE_EXPIRED, CONFERENCE_DENIED,
    CONFERENCE_INVALID, CONFERENCE_MISSING, CONFERENCE_EXISTS, CONFERENCE_STORAGE } ConferenceStatus;
typedef struct { unsigned long session; int slot; uint32_t user,epoch; } ConferenceActor;
typedef struct {
    char name[81];
    int administrator,guest;
} ConferenceIdentity;
typedef ConferenceStatus (*ConferenceResolve)(void *,const ConferenceActor *,ConferenceIdentity *);
typedef struct {
    ConferenceAction action;
    uint32_t room,cursor;
    char text[CONFERENCE_TEXT+1];
} ConferenceRequest;
typedef struct {
    uint32_t id,room,user;
    uint64_t time;
    char name[81],text[CONFERENCE_TEXT+1];
} ConferenceLine;
typedef struct { uint32_t id; char title[81]; } ConferenceRoom;
typedef struct {
    ConferenceStatus status;
    ConferenceAction action;
    uint32_t room,position,next;
    unsigned count;
    union { ConferenceRoom rooms[CONFERENCE_PAGE]; ConferenceLine lines[CONFERENCE_PAGE]; } rows;
} ConferenceReply;

/* Owner-thread only. Identity resolution is required for every call. A zero
 * cursor starts a page, and a zero room means all rooms only for unread/seen. */
int conference_schema(sqlite3 *);
ConferenceStatus conference_run(sqlite3 *,ConferenceResolve,void *,const ConferenceActor *,
                                const ConferenceRequest *,ConferenceReply *);
#endif
