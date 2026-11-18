#ifndef PREFERENCE_CORE_H
#define PREFERENCE_CORE_H
#include <stdint.h>
/* In-memory values; storage validation/authorization remains in Member Core. */
typedef struct {
    int language,pager,editor;
    unsigned columns,rows,line_numbers,transfer_protocol;
} PreferenceValues;
typedef struct {
    int language,pager,editor;
    unsigned columns,rows,aux;
} PreferenceRecord;
typedef enum {
    PREF_EXIT_NONE=0,PREF_EXIT_LOGOUT=1,PREF_EXIT_QUIT=2,PREF_EXIT_SAVED=3
} PreferenceExit;
typedef struct {
    uint64_t baseline;
    PreferenceExit exit_action;
} PreferenceLifecycle;
PreferenceRecord preference_pack(const PreferenceValues *);
/* Legacy records with either dimension zero preserve both current dimensions. */
PreferenceValues preference_unpack(const PreferenceRecord *,const PreferenceValues *current);
uint64_t preference_signature(const PreferenceValues *);
int preference_disconnect_needed(const PreferenceLifecycle *,uint32_t user,const PreferenceValues *);
void preference_observed(PreferenceLifecycle *,const PreferenceValues *);
int preference_exit_begin(PreferenceLifecycle *,uint32_t user,PreferenceExit);
void preference_exit_submitted(PreferenceLifecycle *,int pending);
/* saved is the existing protocol adapter's successful-save decision. */
PreferenceExit preference_exit_complete(PreferenceLifecycle *,int saved);
#endif
