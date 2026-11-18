#ifndef LINER_DOOR_PROTOCOL_H
#define LINER_DOOR_PROTOCOL_H
#include "door_core.h"
/* Legacy RAW/menu/Web connector protocol. Returns 0 and an empty result if
 * output does not fit; never emit an incomplete success frame. */
int door_protocol_list(const DoorListing *,char *,size_t);
#endif
