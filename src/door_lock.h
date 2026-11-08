#ifndef DOOR_LOCK_H
#define DOOR_LOCK_H
/* On success returns an inheritable lease descriptor. The caller retains
 * ownership of fd and closes it if the returned descriptor differs. */
int door_lock_acquire(int fd);
/* Call after the owned Door process group has terminated. */
void door_lock_release(int fd);
void door_lock_reap(void);
#endif
