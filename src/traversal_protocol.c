#include "traversal_protocol.h"
#include <stdio.h>
#include <string.h>
static int safe_path(const char *p) {
    for(;*p;p++)if((unsigned char)*p<32 || *p==127 || *p=='|')return 0;
    return 1;
}
unsigned check_traversal_select(CheckTraversal *c,const char *kind,uint32_t board) {
    c->key[0]=0;c->scope=1;c->board=board;
    unsigned number=!strcmp(kind,"ARTICLES") || !strcmp(kind,"ARTICLES ALL")?0:!strcmp(kind,"MAIL")?1:!strcmp(kind,"FILES")?2:3;
    if(!strcmp(kind,"ARTICLES HERE") || !strcmp(kind,"ARTICLES SUBTREE")) {
        number=0;c->scope=!strcmp(kind,"ARTICLES HERE")?0:2;
        snprintf(c->key,sizeof(c->key),"%u:%u",board,(unsigned)c->scope);
    } else if(!strncmp(kind,"FILES ",6) && strlen(kind+6)<=48 && safe_path(kind+6)) {
        number=2;strcpy(c->key,kind+6);
    }
    return number;
}
void check_traversal_response(CheckTraversal *c,const char *text) {
    const char *end=strstr(text,"END NEXT ");unsigned next=0;
    if(end)sscanf(end,"END NEXT %u",&next);
    check_traversal_page(c,end!=NULL,next);
}
