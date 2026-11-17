#ifndef CATALOG_H
#define CATALOG_H
#include <stdint.h>
#include <stddef.h>
#define CATALOG_MAX 1024
int catalog_under(uint32_t,uint32_t);
int catalog_group(uint32_t,unsigned);
int catalog_replace_group(uint32_t,uint32_t,unsigned,unsigned);
int catalog_sigop(uint32_t board,uint32_t user);
int catalog_manage(uint32_t board,uint32_t user);
int catalog_role_set(uint32_t board,uint32_t user,uint32_t actor,uint32_t *previous);
/* Worker-owned persistence boundary; outputs own all strings. No UI text. */
typedef struct {
    uint32_t id,parent;
    char read_acl[256],write_acl[256],title[81],description[321];
} CatalogConfig;
typedef struct {
    uint32_t sequence,actor,board;uint64_t time;char specification[1024];
} CatalogChange;
int catalog_storage_broken(void);
int catalog_config(uint32_t id,CatalogConfig *out);
int catalog_decode(const char *specification,CatalogConfig *out);
int catalog_role_get(uint32_t board,uint32_t *user);
uint32_t catalog_change_count(void);
int catalog_change_get(uint32_t sequence,CatalogChange *out);
int catalog_load(const char *path);
int catalog_exists(uint32_t id);
int catalog_known(uint32_t id);
int catalog_visible(uint32_t id,uint32_t user);
int catalog_read(uint32_t id,uint32_t user);
void catalog_guest(uint32_t,int);
int catalog_write(uint32_t id,uint32_t user);
uint32_t catalog_parent(uint32_t id);
unsigned catalog_depth(uint32_t id);
const char *catalog_title(uint32_t id);
uint32_t catalog_next(uint32_t id);
const char *catalog_description(uint32_t id);
int catalog_open_changes(const char *base);
void catalog_close_changes(void);
int catalog_set(uint32_t actor,const char *text,uint32_t *id);
#endif
