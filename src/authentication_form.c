#include "authentication_form.h"
#include <string.h>
void authentication_form_decode(const char *body,AuthenticationDecodedForm *out) {
    memset(out,0,sizeof(*out));out->form.fields=out->fields;
    if(!body || strlen(body)>AUTH_FORM_BYTES)return;
    strcpy(out->text,body);char *line=out->text;
    while(*line) {
        char *end=strchr(line,'\n'),*bar=strchr(line,'|');
        if(!end || !bar || bar>end || out->form.count==AUTH_FORM_FIELDS)return;
        *end=0;*bar++=0;int field=profile_field(line);
        if(field<0)field=!strcmp(line,"FROMBBS")?AUTH_FROM_BBS:!strcmp(line,"SYSOPNOTE")?AUTH_SYSOP_NOTE:-1;
        if(field<0)return;
        AuthenticationField *f=&out->fields[out->form.count++];f->field=(unsigned)field;f->value=bar;
        line=end+1;
    }
    out->form.valid=1;
}
