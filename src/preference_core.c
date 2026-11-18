#include "preference_core.h"
PreferenceRecord preference_pack(const PreferenceValues *v) {
    return (PreferenceRecord){v->language,v->pager,v->editor,v->columns,v->rows,
                              v->line_numbers | (v->transfer_protocol<<1)};
}
PreferenceValues preference_unpack(const PreferenceRecord *r,const PreferenceValues *current) {
    PreferenceValues v={r->language,r->pager,r->editor,current->columns,current->rows,r->aux&1,r->aux>>1};
    if(r->columns && r->rows) { v.columns=r->columns;v.rows=r->rows; }
    return v;
}
uint64_t preference_signature(const PreferenceValues *v) {
    return (uint64_t)v->language | ((uint64_t)v->pager<<2) | ((uint64_t)v->editor<<3) |
           ((uint64_t)v->columns<<8) | ((uint64_t)v->rows<<24) |
           ((uint64_t)v->line_numbers<<32) | ((uint64_t)v->transfer_protocol<<33);
}
int preference_disconnect_needed(const PreferenceLifecycle *s,uint32_t user,const PreferenceValues *v) {
    return user && s->exit_action!=PREF_EXIT_SAVED && s->baseline!=preference_signature(v);
}
void preference_observed(PreferenceLifecycle *s,const PreferenceValues *v) {
    s->baseline=preference_signature(v);
}
int preference_exit_begin(PreferenceLifecycle *s,uint32_t user,PreferenceExit action) {
    if(!user || s->exit_action==PREF_EXIT_SAVED)return 0;
    if(action!=PREF_EXIT_LOGOUT && action!=PREF_EXIT_QUIT)return 0;
    s->exit_action=action;return 1;
}
void preference_exit_submitted(PreferenceLifecycle *s,int pending) {
    if(!pending)s->exit_action=PREF_EXIT_NONE;
}
PreferenceExit preference_exit_complete(PreferenceLifecycle *s,int saved) {
    PreferenceExit action=s->exit_action;s->exit_action=PREF_EXIT_NONE;
    if(!saved || action==PREF_EXIT_NONE)return PREF_EXIT_NONE;
    s->exit_action=PREF_EXIT_SAVED;return action;
}
