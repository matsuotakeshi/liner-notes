#define _POSIX_C_SOURCE 200809L
#include "vault_bridge.h"
#include "vault.h"
#include "vault_query_core.h"
#include "vault_management_core.h"
#include "download.h"
#include <stdlib.h>
#include "date_filter.h"
#include "transfer_line.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>
static Vault *store;
/* The vault and board workers may format audit timestamps concurrently. */
static struct tm *utc_time(const time_t *at,struct tm *out) {
#ifdef _WIN32
    return localtime_s(out,at)?NULL:out;
#else
    return localtime_r(at,out);
#endif
}
int vault_bridge_open(const char *directory) { if(vault_open(directory,&store)!=VAULT_OK)return 0;if(!downloads_open(directory)) { vault_close(store);store=NULL;downloads_close();return 0; }return 1; }
void vault_bridge_close(void) { downloads_close();vault_close(store);store=NULL; }
void vault_bridge_execute(IdentityProvider identity,uint32_t user,uint32_t epoch,int op,uint32_t id,const char *argument,BoardResult *response) {
    char *out=response->text;size_t size=sizeof(response->text);BoardFeedback *f=&response->feedback;
    if(!store) { (f->failed=1,snprintf(out,size,"ERR vault disabled\r\n"));return; }
    Principal p={0};
    if(!identity.resolve || !identity.resolve(identity.context,user,epoch,&p)) { (f->failed=1,snprintf(out,size,"ERR login required\r\n"));return; }
    VaultResult r=VAULT_INVALID;VaultEntry e;
    static const struct { int op;VaultFileChangeKind kind;const char *label; } file_changes[]={
        {VAULT_CMD_DESCRIPTION,VAULT_FILE_DESCRIPTION,"EDITED"},{VAULT_CMD_RENAME,VAULT_FILE_RENAME,"RENAMED"},
        {VAULT_CMD_OWNER,VAULT_FILE_OWNER,"OWNER CHANGED"},{VAULT_CMD_ASCII,VAULT_FILE_ASCII,"ATTRIBUTE CHANGED"},
        {VAULT_CMD_CATEGORY,VAULT_FILE_CATEGORY,"CATEGORY"},{VAULT_CMD_HIDE,VAULT_FILE_HIDE,"HIDDEN"},
        {VAULT_CMD_RESTORE,VAULT_FILE_RESTORE,"RESTORED"},{VAULT_CMD_DELETE,VAULT_FILE_DELETE,"DELETED"}
    };
    for(size_t i=0;i<sizeof(file_changes)/sizeof(file_changes[0]);i++)if(op==file_changes[i].op) {
        VaultFileChange change={.kind=file_changes[i].kind,.text=op==VAULT_CMD_CATEGORY && !strcmp(argument,"-")?"":argument,
                               .ascii=op==VAULT_CMD_ASCII?(!strcmp(argument,"ON")?1:!strcmp(argument,"OFF")?0:-1):0};
        r=vault_manage_file(store,&p,id,&change);
        if(r==VAULT_OK) { snprintf(out,size,"FILE %s %u\r\n",file_changes[i].label,id);return; }
        break;
    }
    if(op==VAULT_CMD_PASSWORD || op==VAULT_CMD_UNLOCK || op==VAULT_CMD_OPTIONS) {
        const char *bar=strchr(argument,'|');if(bar && bar-argument<=48) { char name[49];memcpy(name,argument,(size_t)(bar-argument));name[bar-argument]=0;
            const char *path=!strcmp(name,"-")?"":name;
            if(op==VAULT_CMD_UNLOCK) { VaultBoardGrant grant;r=vault_manage_unlock(store,&p,path,bar+1,&grant);if(r==VAULT_OK) { f->file_unlocked=1;f->grant_board=grant.board;f->grant_revision=grant.revision;snprintf(out,size,"FILEBOARD UNLOCKED %u %u\r\n",grant.board,grant.revision);return; } }
            else if(op==VAULT_CMD_PASSWORD)r=vault_manage_password(store,&p,path,bar+1);
            else { VaultBoardUpdate result=vault_manage_board(store,&p,path,VAULT_BOARD_OPTIONS,bar+1);r=result.result;
                if(result.retention_failed) { (f->failed=1,snprintf(out,size,"ERR vault log storage\r\n"));return; } }
        }
        if(r==VAULT_OK) { f->file_updated=1;snprintf(out,size,"FILEBOARD UPDATED\r\n");return; }
    }
    if(op==VAULT_CMD_CHILDREN) {
        VaultQueryChildren page;r=vault_query_children(store,&p,!strcmp(argument,"-")?"":argument,id,&page);
        if(r==VAULT_OK) {
            OPERATION_PAGE(response,FILE_CHILDREN,file_children,page);
            f->file_children=1;f->child_count=(unsigned)page.count;f->child_next=page.next;for(size_t i=0;i<page.count;i++)snprintf(f->children[i],sizeof(f->children[i]),"%s",page.rows[i].name);
            size_t used=(size_t)snprintf(out,size,"FILECHILDREN %s\r\n",argument);
            for(size_t i=0;i<page.count;i++)used+=(size_t)snprintf(out+used,size-used,"CHILD %u %s|%s\r\n",(unsigned)i+1,page.rows[i].name,page.rows[i].display);
            snprintf(out+used,size-used,"END CHILDREN NEXT %u\r\n",page.next);return;
        }
    }
    if(op==VAULT_CMD_COPY) { r=vault_manage_copy(store,&p,id,!strcmp(argument,"-")?"":argument,&e);if(r==VAULT_OK) { f->file_copied=1;snprintf(out,size,"FILE COPIED %u %u\r\n",id,e.id);return; } }
    if(op==VAULT_CMD_MATCH) {
        char category[49];const char *pattern=argument,*scope=NULL,*bar=strchr(argument,'|');
        if(bar) { size_t length=(size_t)(bar-argument);if(!length || length>48) { (f->failed=1,snprintf(out,size,"ERR vault syntax\r\n"));return; }memcpy(category,argument,length);category[length]=0;if(!strcmp(category,"-"))category[0]=0;scope=category;pattern=bar+1; }
        VaultQueryMatch match;r=vault_query_match(store,&p,pattern,scope,&match);
        if(r==VAULT_OK) { f->batch_match=1;f->count=(unsigned)match.count;memcpy(f->ids,match.ids,match.count*sizeof(*match.ids));size_t used=(size_t)snprintf(out,size,"BATCHMATCH");for(size_t i=0;i<match.count;i++)used+=(size_t)snprintf(out+used,size-used," %u",match.ids[i]);snprintf(out+used,size-used,"\r\n");return; }
    }
    if(op==VAULT_CMD_BOARDNAME || op==VAULT_CMD_BOARDGUIDE || op==VAULT_CMD_BOARDSIGOP) {
        const char *bar=strchr(argument,'|');if(bar && bar-argument<=48) { char name[49];memcpy(name,argument,(size_t)(bar-argument));name[bar-argument]=0;VaultBoardUpdate result=vault_manage_board(store,&p,!strcmp(name,"-")?"":name,op==VAULT_CMD_BOARDNAME?VAULT_BOARD_NAME:op==VAULT_CMD_BOARDSIGOP?VAULT_BOARD_SIGOP:VAULT_BOARD_GUIDE,bar+1);r=result.result; }
        if(r==VAULT_OK) { f->file_updated=1;snprintf(out,size,"FILEBOARD UPDATED\r\n");return; }
    }
    if(op==VAULT_CMD_BOARDEDIT) {
        VaultBoard b;r=vault_manage_inspect(store,&p,!strcmp(argument,"-")?"":argument,1,&b);
        if(r==VAULT_OK) { f->file_guide=1;snprintf(response->draft_title,sizeof(response->draft_title),"%s",argument);snprintf(response->draft_body,sizeof(response->draft_body),"%s",b.info);snprintf(out,size,"FILEBOARD EDIT %s\r\n%s",argument,b.info);return; }
    }
    if(op==VAULT_CMD_TEXT) {
        char *end;unsigned long offset=strtoul(argument,&end,10);r=vault_info(store,&p,id,&e);
        if(r==VAULT_OK && (*end || offset>e.size || !e.ascii))r=VAULT_INVALID;
        if(r==VAULT_OK) { size_t used=(size_t)snprintf(out,size,"FILETEXT %u %lu\r\n",id,offset);unsigned chunks=0;
            while(offset<e.size && chunks++<8) { unsigned char data[VAULT_CHUNK];size_t count;r=vault_read(store,&p,id,(uint32_t)offset,data,&count);if(r!=VAULT_OK || !count)break;
                size_t consumed=0;
                while(consumed<count && used+100<size) {
                    unsigned c=data[consumed],n=c<128?1:c>=0xc2 && c<=0xdf?2:c>=0xe0 && c<=0xef?3:c>=0xf0 && c<=0xf4?4:0,cp=n==1?c:n?c&((1u<<(7-n))-1):0;
                    if(n>1 && consumed+n>count && offset+count<e.size)break;
                    int valid=n && consumed+n<=count;
                    for(unsigned j=1;valid && j<n;j++) { unsigned v=data[consumed+j];if((v&0xc0)!=0x80)valid=0;else cp=(cp<<6)|(v&63); }
                    if(!valid || (cp<32 && cp!='\r' && cp!='\n' && cp!='\t') || (cp>=127 && cp<=159) || (n==3 && cp<2048) || (n==4 && cp<65536) || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) { out[used++]='?';consumed+=valid?n:1; }
                    else { memcpy(out+used,data+consumed,n);used+=n;consumed+=n; }
                }
                offset+=(unsigned long)consumed;
            }
            if(r==VAULT_OK) { f->page_found=1;f->page_next=offset<e.size?(uint32_t)offset:0;snprintf(out+used,size-used,"\r\nEND TEXT NEXT %lu\r\n",offset<e.size?offset:0);return; }
        }
    }
    if(op==VAULT_CMD_BOARDINFO) {
        VaultBoard b;r=vault_manage_inspect(store,&p,!strcmp(argument,"-")?"":argument,0,&b);
        if(r==VAULT_OK && !*b.display && !*b.info && !*b.sigop && !b.menu && !b.order && !b.keep && !b.locked) { f->file_board_info=1;f->file_menu=b.menu;snprintf(out,size,"FILEBOARDSETTING %s|%s|%s\r\n",*b.name?b.name:"-",b.read,b.upload);return; }
        if(r==VAULT_OK) { f->file_board_info=1;f->file_menu=b.menu;snprintf(out,size,"FILEBOARDSETTING %s|%s|%s\r\nDISPLAY %s\r\nSIGOP %s\r\n%s",*b.name?b.name:"-",b.read,b.upload,b.display,*b.sigop?b.sigop:"-",b.info);size_t used=strlen(out);snprintf(out+used,size-used,"OPTIONS %s %u %u PASSWORD %s\r\nEND FILEBOARD\r\n",b.menu?"MENU":"BOARD",b.order,b.keep,b.locked?"ON":"OFF");return; }
    }
    if(op==VAULT_CMD_STATS || op==VAULT_CMD_HISTORY) {
        char *end;unsigned long cursor=strtoul(argument,&end,10);if(*end || cursor>UINT32_MAX) { (f->failed=1,snprintf(out,size,"ERR vault syntax\r\n"));return; }
        f->page_found=downloads_report_page(store,&p,op==VAULT_CMD_HISTORY,id,(uint32_t)cursor,out,size,&f->page_next);f->failed=!f->page_found;return;
    }
    if(op==VAULT_CMD_BOARDSET) {
        char copy[256];if(strlen(argument)>=sizeof(copy)) { (f->failed=1,snprintf(out,size,"ERR vault syntax\r\n"));return; }strcpy(copy,argument);
        char *read=strchr(copy,'|'),*write=read?strchr(read+1,'|'):NULL;
        if(read && write && !strchr(write+1,'|')) { *read++=0;*write++=0;r=vault_manage_policy(store,&p,!strcmp(copy,"-")?"":copy,read,write); }
        if(r==VAULT_OK) { f->file_updated=1;snprintf(out,size,"FILEBOARD UPDATED\r\n");return; }
    } else if(op==VAULT_CMD_LIST || op==VAULT_CMD_SEARCH || op==VAULT_CMD_IN || op==VAULT_CMD_NEWS) {
        VaultQueryFiles page;VaultQuery request={.cursor=id};
        char category[49];const char *query=argument,*scope=NULL,*bar=strchr(argument,'|');
        if(bar && (op==VAULT_CMD_SEARCH || op==VAULT_CMD_NEWS)) {
            size_t n=(size_t)(bar-argument);if(n>48 || !n) { (f->failed=1,snprintf(out,size,"ERR vault syntax\r\n"));return; }
            memcpy(category,argument,n);category[n]=0;if(!strcmp(category,"-"))category[0]=0;
            request.check_scope=1;
            scope=category;query=bar+1;
        }
        request.scope=op==VAULT_CMD_IN?(!strcmp(argument,"-")?"":argument):scope;
        request.pattern=op==VAULT_CMD_SEARCH?query:"";
        if(op==VAULT_CMD_NEWS) {
            unsigned long long low,high;int consumed=0;request.news=1;
            if(sscanf(query,"@%llu:%llu%n",&low,&high,&consumed)==2 && !query[consumed]) {
                request.date_valid=1;request.since=(uint64_t)low;request.until=(uint64_t)high;request.has_until=1;
            }else request.date_valid=date_filter(query,&request.since);
        }
        r=vault_query_files(store,&p,&request,&page);
        if(r==VAULT_OK) {
            OPERATION_PAGE(response,FILES,files,page);
            size_t pos=(size_t)(op==VAULT_CMD_LIST?snprintf(out,size,"FILES\r\n"):snprintf(out,size,"%s %s\r\n",op==VAULT_CMD_NEWS?"FILENEWS":op==VAULT_CMD_SEARCH?"FILESEARCH":"FILESIN",op==VAULT_CMD_NEWS && query[0]=='@'?"CHECK":argument));
            for(size_t i=0;i<page.count;i++) { pos+=(size_t)snprintf(out+pos,size-pos,"FILEITEM %u %u %s\r\n",page.rows[i].id,page.rows[i].size,page.rows[i].name);
                pos+=(size_t)snprintf(out+pos,size-pos,"FILEDETAIL %u %s %llu %s\r\n",page.rows[i].id,page.rows[i].ascii?"ASCII":"BINARY",(unsigned long long)downloads_count(page.rows[i].id),page.rows[i].description); }
            f->page_found=1;f->page_next=page.next;snprintf(out+pos,size-pos,"END NEXT %u\r\n",page.next);return;
        }
    } else if(op==VAULT_CMD_INFO) {
        VaultQueryDetail detail;r=vault_query_detail(store,&p,id,&detail);
        if(r==VAULT_OK) {
            OPERATION_PAGE(response,FILE_DETAIL,file_detail,detail); f->file_detail=1;char hash[65];e=detail.entry;vault_hex(e.sha256,hash);snprintf(out,size,"FILE %u %u %s\r\nNAME %s\r\nDESCRIPTION %s\r\nPUBLISHER %s\r\nCATEGORY %s\r\nASCII %s\r\nPROTOCOL %s\r\nEND FILE\r\n",e.id,e.size,hash,e.name,e.description,e.publisher,*e.category?e.category:"-",e.ascii?"ON":"OFF",detail.protocol);return; }
    }
    if(op==VAULT_CMD_BOARDS) {
        VaultQueryBoards page;r=vault_query_boards(store,&p,id,&page);
        if(r==VAULT_OK) {
            OPERATION_PAGE(response,FILE_BOARDS,file_boards,page);
            size_t pos=(size_t)snprintf(out,size,"FILEBOARDS\r\n");
            for(size_t i=0;i<page.count;i++)pos+=(size_t)snprintf(out+pos,size-pos,"VBOARD %d %s%s%s\r\n",page.rows[i].can_upload,*page.rows[i].name?page.rows[i].name:"-",*page.rows[i].display?" | ":"",page.rows[i].display);
            f->page_found=1;f->page_next=page.next;snprintf(out+pos,size-pos,"END NEXT %u\r\n",page.next);return;
        }
    } else if(op==VAULT_CMD_CATEGORIES) {
        VaultQueryCategories page;r=vault_query_categories(store,&p,id,&page);
        if(r==VAULT_OK) {
            OPERATION_PAGE(response,FILE_CATEGORIES,file_categories,page);
            size_t pos=(size_t)snprintf(out,size,"FILECATEGORIES\r\n");
            for(size_t i=0;i<page.count;i++)pos+=(size_t)snprintf(out+pos,size-pos,"VCATEGORY %u %s\r\n",page.rows[i].count,*page.rows[i].name?page.rows[i].name:"-");
            f->page_found=1;f->page_next=page.next;snprintf(out+pos,size-pos,"END NEXT %u\r\n",page.next);return;
        }
    } else if(op==VAULT_CMD_AUDIT) {
        VaultQueryAudit page;r=vault_query_audit(store,&p,id,&page);
        if(r==VAULT_OK) {
            OPERATION_PAGE(response,FILE_AUDIT,file_audit,page);
            size_t pos=(size_t)snprintf(out,size,"FILEAUDIT\r\n");
            for(size_t i=0;i<page.count;i++) {
                char stamp[40]="unknown";time_t at=(time_t)page.rows[i].at;struct tm value;struct tm *utc=page.rows[i].at?utc_time(&at,&value):NULL;
                if(utc)strftime(stamp,sizeof(stamp),date_pattern(),utc);
                pos+=(size_t)snprintf(out+pos,size-pos,"VEVENT %u %u %s %c\t%s\t%s\t%s\r\n",page.rows[i].sequence,page.rows[i].file,stamp,page.rows[i].action,page.rows[i].actor,page.rows[i].action!='E' && *page.rows[i].before?page.rows[i].before:"-",page.rows[i].action!='E' && *page.rows[i].after?page.rows[i].after:"-");
            }
            f->page_found=1;f->page_next=page.next;snprintf(out+pos,size-pos,"END NEXT %u\r\n",page.next);return;
        }
    }
    f->failed=1;const char *message=r==VAULT_DENIED?"ERR vault denied":r==VAULT_NOT_FOUND?"ERR no file":r==VAULT_BUSY?"ERR vault busy":r==VAULT_INVALID?"ERR vault syntax":r==VAULT_LIMIT?"ERR vault limit":"ERR vault storage";
    snprintf(out,size,"%s\r\n",message);
}

void vault_bridge_transfer(IdentityProvider identity,uint32_t user,uint32_t epoch,const char *line,unsigned protocol,BoardResult *response) {
    char *out=response->text;size_t size=sizeof(response->text);
    if(!store) { response->feedback.failed=1;snprintf(out,size,"ERR vault disabled\r\n");return; }
    Principal p={0};
    if(!identity.resolve || !identity.resolve(identity.context,user,epoch,&p)) { response->feedback.failed=1;snprintf(out,size,"ERR login required\r\n");return; }
    const char *protocols[]={"LINE","ZMODEM","XMODEM","YMODEM","YMODEM-G","MLINK","TEXT","XMODEM-SUM","XMODEM-1K"};if(protocol>8)protocol=0;
    if(downloads_command_result(store,&p,line,protocols[protocol],out,size,&response->feedback.failed))return;
    unsigned id=0;int consumed=0;
    if(sscanf(line,"FILEFINISH %u%n",&id,&consumed)==1 && !line[consumed]) {
        VaultResult logged=vault_upload_protocol(store,&p,id,protocols[protocol]);
        if(logged!=VAULT_OK) { response->feedback.failed=1;snprintf(out,size,"ERR vault protocol log %s\r\n",vault_result_name(logged));return; }
    }
    response->feedback.failed=transfer_line_result(vault_transfer_backend(store),&p,line,out,size,&id)!=VAULT_OK;
    if(id) {
        VaultResult logged=vault_upload_protocol(store,&p,id,protocols[protocol]);
        if(logged!=VAULT_OK) { response->feedback.failed=1;snprintf(out,size,"ERR vault protocol log %s reservation %u\r\n",vault_result_name(logged),id); }
    }
}

void vault_bridge_guard(int (*guard)(void *),void *context) { vault_set_guard(store,guard,context);downloads_guard(guard,context); }
static void put_big(unsigned char *p,uint32_t n) { for(int i=3;i>=0;i--) { p[i]=(unsigned char)n;n>>=8; } }
void vault_bridge_binary(IdentityProvider identity,const BoardRequest *q,BoardResult *r) {
    if(!store) { r->feedback.failed=1;strcpy(r->text,"ERR vault disabled\r\n");return; }
    Principal p={0};
    if(!identity.resolve || !identity.resolve(identity.context,q->user,q->user_epoch,&p)) { r->feedback.failed=1;strcpy(r->text,"ERR login required\r\n");return; }
    TransferBackend backend=vault_transfer_backend(store);TransferRequest request={0};TransferReply reply={0};
    request.operation=q->action=='R'?TRANSFER_READ:TRANSFER_WRITE;request.id=q->number;request.offset=q->cursor;
    if(q->scope<0 || q->scope>VAULT_CHUNK) { r->feedback.failed=1;strcpy(r->text,"ERR vault syntax\r\n");return; }
    request.count=(size_t)q->scope;memcpy(request.bytes,q->body,request.count);
    VaultResult result=backend.execute(backend.context,&p,&request,&reply);
    if(result!=VAULT_OK) { r->feedback.failed=1;snprintf(r->text,sizeof(r->text),"%s\r\n",result==VAULT_DENIED?"ERR vault denied":result==VAULT_BUSY?"ERR vault busy":result==VAULT_NOT_FOUND?"ERR no file":result==VAULT_INVALID?"ERR vault syntax":"ERR vault storage");return; }
    if(request.operation==TRANSFER_READ) {
        unsigned char *data=(unsigned char *)r->text;data[0]='D';put_big(data+1,q->number);put_big(data+5,q->cursor);
        memcpy(data+9,reply.bytes,reply.count);r->binary_size=(unsigned)(9+reply.count);
    } else snprintf(r->text,sizeof(r->text),"RECEIVED %u %u\r\n",q->number,reply.received);
}
