#include "form_flow_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void empty_fields(ApplicationForm *f) {
    while(application_form_field(f)) {
        int last=f->step==14;
        assert(application_form_input(f,APPLICATION_VALUE,"",1)==(last?APPLICATION_REVIEW:APPLICATION_PROMPT));
    }
}
static void application_tests(void) {
    const char *keys[]={"REALNAME","KANA","ADDRESS","PHONE","BIRTHDAY","MACHINE","HANDLE","LOCATION","OCCUPATION","INTERESTS","WEBSITE","CONTACT","FROMBBS","SYSOPNOTE"};
    ApplicationForm f={0},other={0};char too_long[322];memset(too_long,'a',321);too_long[321]=0;
    assert(application_form_input(&f,APPLICATION_VALUE,"ignored",1)==APPLICATION_INACTIVE);
    application_form_begin(&f);assert(f.step==1 && !f.body[0]);
    for(unsigned i=0;i<14;i++) {
        assert(!strcmp(application_form_field(&f),keys[i]));
        char saved[APPLICATION_FORM_CAPACITY];memcpy(saved,f.body,sizeof(saved));
        assert(application_form_input(&f,APPLICATION_VALUE,"bad",0)==APPLICATION_INVALID);
        assert(application_form_input(&f,APPLICATION_VALUE,too_long,1)==APPLICATION_INVALID);
        assert(application_form_input(&f,APPLICATION_VALUE,"bad\nvalue",1)==APPLICATION_INVALID);
        assert(application_form_input(&f,APPLICATION_VALUE,"bad\177value",1)==APPLICATION_INVALID);
        assert(f.step==(int)i+1 && !memcmp(saved,f.body,sizeof(saved)));
        /* Y/E are values before the confirmation step, not control actions. */
        assert(application_form_input(&f,i%2?APPLICATION_CONFIRM:APPLICATION_RESTART,i%2?"Y":"E",1)==(i==13?APPLICATION_REVIEW:APPLICATION_PROMPT));
    }
    assert(f.step==15 && !application_form_field(&f));
    assert(!strcmp(f.body,"REALNAME|E\nKANA|Y\nADDRESS|E\nPHONE|Y\nBIRTHDAY|E\nMACHINE|Y\nHANDLE|E\nLOCATION|Y\nOCCUPATION|E\nINTERESTS|Y\nWEBSITE|E\nCONTACT|Y\nFROMBBS|E\nSYSOPNOTE|Y\n"));
    assert(application_form_input(&f,APPLICATION_VALUE,"N",0)==APPLICATION_CONFIRM_PROMPT && f.step==15);
    assert(application_form_input(&f,APPLICATION_RESTART,"E",1)==APPLICATION_PROMPT && f.step==1 && !f.body[0]);
    empty_fields(&f);assert(!strstr(f.body,"|E") && !strstr(f.body,"|Y"));
    assert(application_form_input(&f,APPLICATION_CONFIRM,"Y",1)==APPLICATION_PASSWORD && !f.step && f.body[0]);
    assert(application_form_input(&f,APPLICATION_CONFIRM,"Y",1)==APPLICATION_INACTIVE);
    application_form_clear(&f);
    const unsigned char *bytes=(const unsigned char *)&f;
    for(size_t i=0;i<sizeof(f);i++)assert(bytes[i]==0);
    application_form_begin(&f);
    assert(application_form_input(&f,APPLICATION_VALUE,"personal",1)==APPLICATION_PROMPT);
    assert(application_form_input(&f,APPLICATION_CANCEL,"/CANCEL",1)==APPLICATION_ABORTED);
    for(size_t i=0;i<sizeof(f);i++)assert(bytes[i]==0);
    application_form_begin(&f);empty_fields(&f);
    assert(application_form_input(&f,APPLICATION_CANCEL,"/CANCEL",1)==APPLICATION_ABORTED);
    for(size_t i=0;i<sizeof(f);i++)assert(bytes[i]==0);
    application_form_begin(&f);too_long[320]=0;
    for(unsigned i=0;i<14;i++)assert(application_form_input(&f,APPLICATION_VALUE,too_long,1)==(i==13?APPLICATION_REVIEW:APPLICATION_PROMPT));
    assert(strlen(f.body)<sizeof(f.body) && strstr(f.body,"SYSOPNOTE|"));
    assert(!other.step && !other.body[0]);
    /* Refuse an append without altering an already full caller-owned value. */
    application_form_begin(&f);memset(f.body,'a',sizeof(f.body)-1);f.body[sizeof(f.body)-1]=0;
    assert(application_form_input(&f,APPLICATION_VALUE,"x",1)==APPLICATION_INVALID && f.step==1 && strlen(f.body)==sizeof(f.body)-1);
    /* Include delimiters and the NUL in the exact-capacity boundary. */
    const char record[]="REALNAME|x\n";
    size_t prefix=sizeof(f.body)-sizeof(record);
    application_form_begin(&f);memset(f.body,'p',prefix);f.body[prefix]=0;
    assert(application_form_input(&f,APPLICATION_VALUE,"x",1)==APPLICATION_PROMPT);
    assert(f.step==2 && strlen(f.body)==sizeof(f.body)-1);
    assert(!memcmp(f.body+prefix,record,sizeof(record)));
    for(size_t i=0;i<prefix;i++)assert(f.body[i]=='p');
    application_form_begin(&f);memset(f.body,'p',prefix+1);f.body[prefix+1]=0;
    ApplicationForm before=f;
    assert(application_form_input(&f,APPLICATION_VALUE,"x",1)==APPLICATION_INVALID);
    assert(!memcmp(&f,&before,sizeof(f)));
}
static void profile_tests(void) {
    const char *keys[]={"HANDLE","BIO","LOCATION","OCCUPATION","INTERESTS","WEBSITE","CONTACT","MACHINE","BIRTHDAY","REALNAME","KANA","ADDRESS","PHONE","GROUP"};
    ProfileForm f={0},other={0};
    for(int search=0;search<=1;search++)for(unsigned n=1;n<=profile_form_count(search);n++) {
        profile_form_target(&f,42);assert(profile_form_select(&f,search,n));
        assert(f.field==n-1 && !strcmp(profile_form_field(f.field),keys[n-1]));
        ProfileFormRequest q=profile_form_request(&f,search);
        assert(q.kind==(search?PROFILE_FORM_SEARCH:PROFILE_FORM_MEMBER));
        assert(q.target==(search?0u:42u) && !strcmp(q.field,keys[n-1]));
        assert(!profile_form_select(&f,search,0) && f.field==n-1);
        assert(!profile_form_select(&f,search,profile_form_count(search)+1) && f.field==n-1);
        assert(!profile_form_select(&f,search,UINT32_MAX) && f.field==n-1);
        profile_form_target(&f,0);q=profile_form_request(&f,search);
        assert(q.kind==(search?PROFILE_FORM_SEARCH:PROFILE_FORM_SELF) && !q.target && f.field==n-1);
    }
    assert(profile_form_count(0)==13 && profile_form_count(1)==14);
    assert(!profile_form_field(14) && !profile_form_field(UINT32_MAX));
    assert(!other.target && !other.field);
}
int main(void) { application_tests();profile_tests();puts("PASS: application field/review/restart/cancel/erase/capacity and profile selection/target/search isolation");return 0; }
