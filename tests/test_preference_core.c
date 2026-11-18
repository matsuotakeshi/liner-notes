#include "preference_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void same_values(const PreferenceValues *a,const PreferenceValues *b) {
    assert(a->language==b->language && a->pager==b->pager && a->editor==b->editor);
    assert(a->columns==b->columns && a->rows==b->rows);
    assert(a->line_numbers==b->line_numbers && a->transfer_protocol==b->transfer_protocol);
}
int main(void) {
    const unsigned widths[]={20,80,240},heights[]={5,24,100};
    for(unsigned bits=0;bits<16;bits++)for(unsigned protocol=0;protocol<7;protocol++)
    for(unsigned x=0;x<3;x++)for(unsigned y=0;y<3;y++) {
        PreferenceValues v={(int)(bits&1),(int)((bits>>1)&1),(int)((bits>>2)&1),widths[x],heights[y],bits>>3,protocol};
        PreferenceValues before=v;
        PreferenceRecord record=preference_pack(&v);
        assert(record.aux==(bits>>3 | protocol<<1));
        PreferenceValues decoded=preference_unpack(&record,&v);same_values(&v,&decoded);same_values(&v,&before);
        uint64_t expected=(uint64_t)v.language | ((uint64_t)v.pager<<2) | ((uint64_t)v.editor<<3) |
                          ((uint64_t)v.columns<<8) | ((uint64_t)v.rows<<24) | ((uint64_t)v.line_numbers<<32) | ((uint64_t)protocol<<33);
        assert(preference_signature(&v)==expected);
        PreferenceLifecycle state={0};assert(preference_disconnect_needed(&state,2,&v));
        assert(!preference_disconnect_needed(&state,0,&v));preference_observed(&state,&v);
        assert(!preference_disconnect_needed(&state,2,&v));
        PreferenceValues altered=v;altered.language^=1;assert(preference_disconnect_needed(&state,2,&altered));
        altered=v;altered.pager^=1;assert(preference_disconnect_needed(&state,2,&altered));
        altered=v;altered.editor^=1;assert(preference_disconnect_needed(&state,2,&altered));
        altered=v;altered.line_numbers^=1;assert(preference_disconnect_needed(&state,2,&altered));
        altered=v;altered.transfer_protocol=(protocol+1)%7;assert(preference_disconnect_needed(&state,2,&altered));
        altered=v;altered.columns++;assert(preference_disconnect_needed(&state,2,&altered));
        altered=v;altered.rows++;assert(preference_disconnect_needed(&state,2,&altered));
        record.columns=0;decoded=preference_unpack(&record,&altered);assert(decoded.columns==altered.columns && decoded.rows==altered.rows);
        record.columns=120;record.rows=0;decoded=preference_unpack(&record,&altered);assert(decoded.columns==altered.columns && decoded.rows==altered.rows);
    }
    for(PreferenceExit action=PREF_EXIT_LOGOUT;action<=PREF_EXIT_QUIT;action++) {
        PreferenceValues v={1,1,1,80,24,1,3};PreferenceLifecycle a={0},b={0};
        preference_observed(&a,&v);uint64_t baseline=a.baseline;
        assert(!preference_exit_begin(&a,0,action) && a.exit_action==PREF_EXIT_NONE);
        assert(!preference_exit_begin(&a,2,PREF_EXIT_NONE) && !a.exit_action);
        assert(preference_exit_begin(&a,2,action));preference_exit_submitted(&a,0);
        assert(!a.exit_action && a.baseline==baseline);
        assert(preference_exit_begin(&a,2,action));preference_exit_submitted(&a,1);assert(a.exit_action==action);
        assert(preference_exit_complete(&a,0)==PREF_EXIT_NONE && !a.exit_action);
        assert(preference_exit_begin(&a,2,action));
        assert(preference_exit_complete(&a,1)==action && a.exit_action==PREF_EXIT_SAVED);
        v.rows=25;assert(!preference_disconnect_needed(&a,2,&v));
        assert(!preference_exit_begin(&a,2,action));
        assert(!b.exit_action && !b.baseline && preference_disconnect_needed(&b,2,&v));
        /* Exit routing clears the recursion guard; no baseline update is inferred. */
        a.exit_action=PREF_EXIT_NONE;assert(preference_disconnect_needed(&a,2,&v));
        assert(preference_exit_complete(&a,1)==PREF_EXIT_NONE && !a.exit_action);
    }
    puts("PASS: 1008 preference encodings, legacy dimensions, per-field dirty checks, exit queue/save failures, recursion guard and session isolation");
    return 0;
}
