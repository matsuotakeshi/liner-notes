#ifndef LN_STORAGE_SQLITE_H
#define LN_STORAGE_SQLITE_H
#include <sys/types.h>
#include <sys/stat.h>
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
/* Authoritative SQLite storage for portable record streams. Binary vault payloads
 * stay outside the DB. Encodings remain explicit and independent of C struct ABI. */
int storage_init(const char *);
int storage_init_structured(const char *);
const char *storage_database(void);
void storage_shutdown(void);
int storage_open(const char *,int,mode_t);
ssize_t storage_read(int,void *,size_t);
ssize_t storage_write(int,const void *,size_t);
off_t storage_seek(int,off_t,int);
int storage_sync(int);
int storage_close(int);
int storage_fstat(int,struct stat *);
int storage_stat(const char *,struct stat *);
int storage_rename(const char *,const char *);
int storage_signature(int,unsigned char [32]);
int storage_probe(const char *,void *,size_t);
int storage_lock(int,struct flock *);
#endif
