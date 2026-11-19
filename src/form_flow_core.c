#include "form_flow_core.h"
#include <stdio.h>
#include <string.h>
static const char *application_fields[]={"REALNAME","KANA","ADDRESS","PHONE","BIRTHDAY","MACHINE","HANDLE","LOCATION","OCCUPATION","INTERESTS","WEBSITE","CONTACT","FROMBBS","SYSOPNOTE"};
static const char *profile_fields[]={"HANDLE","BIO","LOCATION","OCCUPATION","INTERESTS","WEBSITE","CONTACT","MACHINE","BIRTHDAY","REALNAME","KANA","ADDRESS","PHONE","GROUP"};
_Static_assert(sizeof(application_fields)/sizeof(*application_fields)==APPLICATION_FORM_FIELDS,"application fields");
void application_form_begin(ApplicationForm *f) { f->step=1;f->body[0]=0; }
void application_form_clear(ApplicationForm *f) {
    volatile unsigned char *p=(volatile unsigned char *)f;
    for(size_t n=0;n<sizeof(*f);n++)p[n]=0;
}
const char *application_form_field(const ApplicationForm *f) {
    return f->step>=1 && (unsigned)f->step<=APPLICATION_FORM_FIELDS?application_fields[f->step-1]:NULL;
}
ApplicationEffect application_form_input(ApplicationForm *f,ApplicationInput input,const char *value,int valid) {
    if(!f->step)return APPLICATION_INACTIVE;
    if(input==APPLICATION_CANCEL) { application_form_clear(f);return APPLICATION_ABORTED; }
    if(f->step==(int)APPLICATION_FORM_FIELDS+1) {
        if(input==APPLICATION_CONFIRM) { f->step=0;return APPLICATION_PASSWORD; }
        if(input==APPLICATION_RESTART) { application_form_begin(f);return APPLICATION_PROMPT; }
        return APPLICATION_CONFIRM_PROMPT;
    }
    const char *field=application_form_field(f);
    if(!field || !valid || strlen(value)>320)return APPLICATION_INVALID;
    for(const unsigned char *p=(const unsigned char *)value;*p;p++)if(*p<32 || *p==127)return APPLICATION_INVALID;
    size_t used=strlen(f->body),required=strlen(field)+strlen(value)+2;
    if(used>=sizeof(f->body) || required>=sizeof(f->body)-used)return APPLICATION_INVALID;
    snprintf(f->body+used,sizeof(f->body)-used,"%s|%s\n",field,value);
    return ++f->step>(int)APPLICATION_FORM_FIELDS?APPLICATION_REVIEW:APPLICATION_PROMPT;
}
unsigned profile_form_count(int search) { return search?14u:13u; }
const char *profile_form_field(unsigned index) { return index<14?profile_fields[index]:NULL; }
void profile_form_target(ProfileForm *f,uint32_t target) { f->target=target; }
int profile_form_select(ProfileForm *f,int search,uint32_t selection) {
    if(!selection || selection>profile_form_count(search))return 0;
    f->field=selection-1;return 1;
}
ProfileFormRequest profile_form_request(const ProfileForm *f,int search) {
    ProfileFormRequest q={search?PROFILE_FORM_SEARCH:f->target?PROFILE_FORM_MEMBER:PROFILE_FORM_SELF,
        search?0:f->target,profile_form_field(f->field)};
    return q;
}
