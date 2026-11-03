#ifndef LINER_DOORS_H
#define LINER_DOORS_H
#include <stdint.h>
#include <stddef.h>
typedef struct Door Door;
int doors_config(const char *);
void doors_list(unsigned,char *,size_t);
Door *door_start(unsigned,unsigned,uint32_t,const char *,unsigned,unsigned,uint64_t);
int door_input(Door *,unsigned char);
int door_room(Door *);
int door_pump(Door *,int (*)(void *,const unsigned char *,size_t),void *,uint64_t);
/* Effective session size; success includes an unchanged PTY size. */
int door_resize(Door *,unsigned,unsigned);
void door_stop(Door *);
void doors_reap(void);
int doors_pending(void);
#endif
