#!/bin/sh
# The configured Door working directory is the installed native Clans world.
# Use an explicit relative filename: OpenDoors info_path is only 60 bytes.
set -eu
: "${LINER_DOOR_NODE:?requires a configured Door slot}"
: "${LINER_DOOR_DROP_DIR:?requires dorinfo1 mode}"
drop=${LINER_DOOR_DROP_DIR##*/}
case "$drop" in .liner-door-run-*) ;; *) exit 64 ;; esac
case "$LINER_DOOR_NODE" in ''|*[!0-9]*|0) exit 64 ;; esac
test -r "$drop/DORINFO1.DEF"
exec ./clans "/N${LINER_DOOR_NODE}" "/D${drop}/DORINFO1.DEF" /CP437UTF8
