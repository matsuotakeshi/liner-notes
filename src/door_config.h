#ifndef LINER_DOOR_CONFIG_H
#define LINER_DOOR_CONFIG_H
#include "door_core.h"
#include <stdio.h>
/* Startup-only adapter; never reload concurrently with catalogue consumers. */
/* Borrows the stream without closing it; same fail-closed load contract. */
int door_config_read(DoorCatalog *,FILE *,DoorPathCheck,void *);
int door_config_load(DoorCatalog *,const char *,DoorPathCheck,void *);
#endif
