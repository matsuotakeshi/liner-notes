#ifndef LINER_DOOR_CORE_H
#define LINER_DOOR_CORE_H
#include <stddef.h>
#include <stdint.h>

#define DOOR_CATALOG_LIMIT 32
#define DOOR_CONFIG_LINE_SIZE 4096
/* Administrator-owned launch specification. Never send paths to a UI client. */
typedef struct {
    unsigned id,level,seconds,slots,drop;
    char exe[1024],dir[1024],title[161];
} DoorSpec;
typedef struct { DoorSpec games[DOOR_CATALOG_LIMIT];unsigned count; } DoorCatalog;
/* Copy-owned, filtered metadata; independent of the configuration lifetime. */
typedef struct { unsigned id,seconds;char title[161]; } DoorSummary;
typedef struct { DoorSummary games[DOOR_CATALOG_LIMIT];unsigned count; } DoorListing;
typedef int (*DoorPathCheck)(void *,const DoorSpec *);

void door_core_clear(DoorCatalog *);
/* Add one line (without CR/LF). Blank/comment lines are accepted. On any error
 * the whole catalog is cleared, preserving startup's fail-closed contract.
 * The platform adapter must validate executable/directory before acceptance. */
int door_core_add(DoorCatalog *,const char *,DoorPathCheck,void *);
/* Caller obtains current level from the authenticated worker. This is not an
 * authentication API. Transport adapters must not accept a client-supplied level. */
void door_core_list(const DoorCatalog *,unsigned,DoorListing *);
/* Borrowed immutable spec; valid until catalog reload. No process is launched.
 * Platform adapter provides effective-root status and retains all PTY/slot checks. */
const DoorSpec *door_core_launch(const DoorCatalog *,unsigned,unsigned,uint32_t,int);
#endif
