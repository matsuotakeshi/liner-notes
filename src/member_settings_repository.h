#ifndef MEMBER_SETTINGS_REPOSITORY_H
#define MEMBER_SETTINGS_REPOSITORY_H
#include <stdint.h>
/* Internal persistence boundary. Core supplies an authenticated owner and a
 * validated legacy scope key. Missing rows are successful defaults, SQL/type
 * errors are failures. Single board worker owns the SQLite connection. */
int member_settings_repository_ready(void);
int member_settings_repository_now(uint64_t *stamp);
int member_settings_repository_menu_get(uint32_t user,unsigned *mode);
int member_settings_repository_menu_save(uint32_t user,unsigned mode);
int member_settings_repository_ui_get(uint32_t user,char out[32]);
int member_settings_repository_ui_save(uint32_t user,const char *name);
int member_settings_repository_check_get(uint32_t user,unsigned kind,const char *scope,uint64_t *stamp);
int member_settings_repository_check_save(uint32_t user,unsigned kind,const char *scope,uint64_t stamp);
#endif
