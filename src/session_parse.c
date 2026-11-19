#define _POSIX_C_SOURCE 200809L
#include "editor_core.h"
#include "batch_protocol.h"
#include "list_navigation_protocol.h"
#include "traversal_protocol.h"
#include "editor_commands.h"
#include "editor_view.h"
#include "classic_input.h"
#include "terminal_pager.h"
#include "connector_protocol.h"
#include "runtime_limits.h"
#include "app_limits.h"
#include "activity.h"
#include "vault.h"
#include "session_internal.h"
#include "members.h"
#include "catalog.h"
#include "store_limits.h"
#include "profile_store.h"
#include "menu.h"
#include "chat.h"
#include "date_filter.h"
#include "vault_bridge.h"
#include "transfer_line.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <openssl/crypto.h>

int session_impl_number_arg(const char *p,uint32_t *number,const char **rest) {
    if(*p<'0' || *p>'9') return 0;
    uint32_t n=0;
    while(*p>='0' && *p<='9') { unsigned digit=(unsigned)(*p++-'0');if(n>(CONTENT_ID_MAX+1-digit)/10)return 0;n=n*10+digit; }
    if(!n) return 0;
    *number=n; *rest=p; return 1;
}

int session_impl_audit_number(const char *p,uint32_t *out) {
    if(!*p) return 0;
    uint32_t n=0;
    for(;*p;p++) {
        if(*p<'0' || *p>'9') return 0;
        unsigned digit=(unsigned)(*p-'0');
        if(n>(UINT32_MAX-digit)/10) return 0;
        n=n*10+digit;
    }
    if(!n) return 0;
    *out=n; return 1;
}

int session_impl_audit_arguments(const char *args,BoardRequest *q) {
    if(!strcmp(args,"ALL")) return 1;
    char copy[256]; if(!*args || strlen(args)>=sizeof(copy)) return 0;
    strcpy(copy,args); char *tokens[8],*p=copy; unsigned count=0;
    while(*p) {
        if(count==8 || *p==' ') return 0;
        tokens[count++]=p;
        while(*p && *p!=' ') p++;
        if(*p) { *p++=0; if(!*p) return 0; }
    }
    if(!count)return 0;
    unsigned i=0; int actor_set=0;
    if(strcmp(tokens[0],"ARTICLE") && strcmp(tokens[0],"ACTOR") && strcmp(tokens[0],"ACTORID")) {
        if(!session_impl_audit_number(tokens[0],&q->number)) return 0;
        i++;
    }
    while(i<count) {
        const char *key=tokens[i++]; if(i==count) return 0;
        const char *value=tokens[i++];
        if(!strcmp(key,"ARTICLE")) {
            if(q->audit_article || !session_impl_audit_number(value,&q->audit_article) || q->audit_article>CONTENT_ID_MAX) return 0;
        } else if(!strcmp(key,"ACTORID")) {
            if(actor_set++ || !session_impl_audit_number(value,&q->audit_actor) || q->audit_actor>MEMBER_MAX) return 0;
        } else if(!strcmp(key,"ACTOR")) {
            if(actor_set++ || strlen(value)>32) return 0;
            for(const unsigned char *v=(const unsigned char *)value;*v;v++) if(*v<33 || *v>126) return 0;
            strcpy(q->title,value);
        } else return 0;
    }
    return 1;
}

void session_impl_audit_command(const Ui *ui,uint32_t cursor,char *out,size_t size) {
    size_t n=(size_t)snprintf(out,size,"AUDIT");
    if(cursor) n+=(size_t)snprintf(out+n,size-n," %u",cursor);
    if(ui->audit_article) n+=(size_t)snprintf(out+n,size-n," ARTICLE %u",ui->audit_article);
    if(ui->audit_actor) snprintf(out+n,size-n," ACTORID %u",ui->audit_actor);
}

int session_impl_safe_text(const char *p) {
    for(;*p;p++) if((unsigned char)*p<32 || (unsigned char)*p==127) return 0;
    return 1;
}

int session_impl_find_arguments(const char *arg,BoardRequest *q) {
    const char *p=strchr(arg,' '); if(!p) return 0;
    size_t n=(size_t)(p-arg);
    if(n==3 && !strncmp(arg,"ALL",3)) q->scope=1;
    else if(n==4 && !strncmp(arg,"HERE",4)) q->scope=0;
    else if(n==7 && !strncmp(arg,"SUBTREE",7)) q->scope=2;
    else return 0;
    arg=p+1; p=strchr(arg,' '); if(!p) return 0; n=(size_t)(p-arg);
    if(!n || n>32 || !p[1] || strlen(p+1)>80 || !session_impl_safe_text(p+1)) return 0;
    memcpy(q->username,arg,n); q->username[n]=0; strcpy(q->title,p+1); return 1;
}
