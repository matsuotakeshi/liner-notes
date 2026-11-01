#define _POSIX_C_SOURCE 200809L
#include "vault_bridge.h"
#include "vault.h"
#include "runtime_limits.h"
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
void vault_bridge_execute(IdentityProvider identity,uint32_t user,uint32_t epoch,int op,uint32_t id,const char *argument,char *out,size_t size) {
    if(!store) { snprintf(out,size,"ERR vault disabled\r\n");return; }
    Principal p={0};
    if(!identity.resolve || !identity.resolve(identity.context,user,epoch,&p)) { snprintf(out,size,"ERR login required\r\n");return; }
    VaultResult r=VAULT_INVALID;VaultEntry e;
    if(op==VAULT_CMD_PASSWORD || op==VAULT_CMD_UNLOCK || op==VAULT_CMD_OPTIONS) {
        const char *bar=strchr(argument,'|');if(bar && bar-argument<=48) { char name[49];memcpy(name,argument,(size_t)(bar-argument));name[bar-argument]=0;
            if(op==VAULT_CMD_UNLOCK) { uint32_t board,revision;r=vault_unlock(store,&p,!strcmp(name,"-")?"":name,bar+1,&board,&revision);if(r==VAULT_OK) { snprintf(out,size,"FILEBOARD UNLOCKED %u %u\r\n",board,revision);return; } }
            else r=op==VAULT_CMD_PASSWORD?vault_password(store,&p,!strcmp(name,"-")?"":name,bar+1):vault_board_metadata(store,&p,!strcmp(name,"-")?"":name,'v',bar+1);
        }
        if(r==VAULT_OK) { if(op==VAULT_CMD_OPTIONS && !downloads_prune(store)) { snprintf(out,size,"ERR vault log storage\r\n");return; }snprintf(out,size,"FILEBOARD UPDATED\r\n");return; }
    }
    if(op==VAULT_CMD_CHILDREN) {
        const char *parent=!strcmp(argument,"-")?"":argument;size_t plen=strlen(parent),used=(size_t)snprintf(out,size,"FILECHILDREN %s\r\n",argument);
        if(plen>48) { snprintf(out,size,"ERR vault syntax\r\n");return; }
        if(plen) { VaultBoard board;if(vault_board_get(store,&p,parent,&board)!=VAULT_OK) { snprintf(out,size,"ERR vault denied\r\n");return; } }
        uint32_t cursor=id;unsigned shown=0;
        do { VaultBoard boards[8];size_t n;r=vault_boards(store,&p,cursor,boards,&n,&cursor);if(r!=VAULT_OK)break;
            for(size_t i=0;i<n;i++) {
                const char *name=boards[i].name;if(!*name || (plen && (strncmp(name,parent,plen) || name[plen]!='/')))continue;
                const char *child=name+(plen?plen+1:0);if(strchr(child,'/'))continue;
                used+=(size_t)snprintf(out+used,size-used,"CHILD %u %s|%s\r\n",++shown,name,*boards[i].display?boards[i].display:child);
            }
        }while(cursor && shown<8);
        if(r==VAULT_OK) { snprintf(out+used,size-used,"END CHILDREN NEXT %u\r\n",cursor);return; }
    }
    if(op==VAULT_CMD_COPY) { r=vault_copy(store,&p,id,!strcmp(argument,"-")?"":argument,&e);if(r==VAULT_OK) { snprintf(out,size,"FILE COPIED %u %u\r\n",id,e.id);return; } }
    if(op==VAULT_CMD_RENAME) { r=vault_rename(store,&p,id,argument);if(r==VAULT_OK) { snprintf(out,size,"FILE RENAMED %u\r\n",id);return; } }
    if(op==VAULT_CMD_MATCH) {
        uint32_t cursor=0,ids[16];unsigned count=0;uint64_t total=0;
        char category[49];const char *pattern=argument,*scope=NULL,*bar=strchr(argument,'|');
        if(bar) { size_t length=(size_t)(bar-argument);if(!length || length>48) { snprintf(out,size,"ERR vault syntax\r\n");return; }memcpy(category,argument,length);category[length]=0;if(!strcmp(category,"-"))category[0]=0;scope=category;pattern=bar+1; }
        do { VaultEntry rows[8];size_t n;r=vault_search(store,&p,cursor,pattern,scope,rows,&n,&cursor);if(r!=VAULT_OK)break;
            for(size_t i=0;i<n;i++) { if(scope && strcmp(rows[i].category,scope))continue;if(count>=ln_limit("BATCH_FILES",16) || rows[i].size>ln_limit("FILE",1073741824u) || total+rows[i].size>ln_limit("BATCH_BYTES",VAULT_BATCH_BYTES)) { r=VAULT_LIMIT;break; }ids[count++]=rows[i].id;total+=rows[i].size; }
            if(r!=VAULT_OK)break;
        }while(cursor);
        if(r==VAULT_OK) { size_t used=(size_t)snprintf(out,size,"BATCHMATCH");for(unsigned i=0;i<count;i++)used+=(size_t)snprintf(out+used,size-used," %u",ids[i]);snprintf(out+used,size-used,"\r\n");return; }
    }
    if(op==VAULT_CMD_OWNER || op==VAULT_CMD_ASCII) {
        r=op==VAULT_CMD_OWNER?vault_owner(store,&p,id,argument):vault_ascii(store,&p,id,!strcmp(argument,"ON")?1:!strcmp(argument,"OFF")?0:-1);
        if(r==VAULT_OK) { snprintf(out,size,"FILE %s %u\r\n",op==VAULT_CMD_OWNER?"OWNER CHANGED":"ATTRIBUTE CHANGED",id);return; }
    }
    if(op==VAULT_CMD_BOARDNAME || op==VAULT_CMD_BOARDGUIDE || op==VAULT_CMD_BOARDSIGOP) {
        const char *bar=strchr(argument,'|');if(bar && bar-argument<=48) { char name[49];memcpy(name,argument,(size_t)(bar-argument));name[bar-argument]=0;r=vault_board_metadata(store,&p,!strcmp(name,"-")?"":name,op==VAULT_CMD_BOARDNAME?'n':op==VAULT_CMD_BOARDSIGOP?'o':'i',bar+1); }
        if(r==VAULT_OK) { snprintf(out,size,"FILEBOARD UPDATED\r\n");return; }
    }
    if(op==VAULT_CMD_BOARDEDIT) {
        VaultBoard b;r=vault_board_get(store,&p,!strcmp(argument,"-")?"":argument,&b);
        if(r==VAULT_OK && !b.can_manage)r=VAULT_DENIED;
        if(r==VAULT_OK) { snprintf(out,size,"FILEBOARD EDIT %s\r\n%s",argument,b.info);return; }
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
            if(r==VAULT_OK) { snprintf(out+used,size-used,"\r\nEND TEXT NEXT %lu\r\n",offset<e.size?offset:0);return; }
        }
    }
    if(op==VAULT_CMD_BOARDINFO) {
        VaultBoard b;r=vault_board_get(store,&p,!strcmp(argument,"-")?"":argument,&b);
        if(r==VAULT_OK && !*b.display && !*b.info && !*b.sigop && !b.menu && !b.order && !b.keep && !b.locked) { snprintf(out,size,"FILEBOARDSETTING %s|%s|%s\r\n",*b.name?b.name:"-",b.read,b.upload);return; }
        if(r==VAULT_OK) { snprintf(out,size,"FILEBOARDSETTING %s|%s|%s\r\nDISPLAY %s\r\nSIGOP %s\r\n%s",*b.name?b.name:"-",b.read,b.upload,b.display,*b.sigop?b.sigop:"-",b.info);size_t used=strlen(out);snprintf(out+used,size-used,"OPTIONS %s %u %u PASSWORD %s\r\nEND FILEBOARD\r\n",b.menu?"MENU":"BOARD",b.order,b.keep,b.locked?"ON":"OFF");return; }
    }
    if(op==VAULT_CMD_STATS || op==VAULT_CMD_HISTORY) {
        char *end;unsigned long cursor=strtoul(argument,&end,10);if(*end || cursor>UINT32_MAX) { snprintf(out,size,"ERR vault syntax\r\n");return; }
        downloads_report(store,&p,op==VAULT_CMD_HISTORY,id,(uint32_t)cursor,out,size);return;
    }
    if(op==VAULT_CMD_BOARDSET) {
        char copy[256];if(strlen(argument)>=sizeof(copy)) { snprintf(out,size,"ERR vault syntax\r\n");return; }strcpy(copy,argument);
        char *read=strchr(copy,'|'),*write=read?strchr(read+1,'|'):NULL;
        if(read && write && !strchr(write+1,'|')) { *read++=0;*write++=0;r=vault_board_set(store,&p,!strcmp(copy,"-")?"":copy,read,write); }
        if(r==VAULT_OK) { snprintf(out,size,"FILEBOARD UPDATED\r\n");return; }
    } else if(op==VAULT_CMD_DELETE) {
        r=vault_reclaim(store,&p,id);
        if(r==VAULT_OK) { snprintf(out,size,"FILE DELETED %u\r\n",id);return; }
    } else if(op==VAULT_CMD_LIST || op==VAULT_CMD_SEARCH || op==VAULT_CMD_IN || op==VAULT_CMD_NEWS) {
        VaultEntry entries[8];size_t count;uint32_t next;
        char category[49];const char *query=argument,*scope=NULL,*bar=strchr(argument,'|');
        if(bar && (op==VAULT_CMD_SEARCH || op==VAULT_CMD_NEWS)) {
            size_t n=(size_t)(bar-argument);if(n>48 || !n) { snprintf(out,size,"ERR vault syntax\r\n");return; }
            memcpy(category,argument,n);category[n]=0;if(!strcmp(category,"-"))category[0]=0;
            VaultBoard selected;if(vault_board_get(store,&p,category,&selected)!=VAULT_OK) { snprintf(out,size,"ERR vault denied\r\n");return; }
            scope=category;query=bar+1;
        }
        if(op==VAULT_CMD_NEWS) { uint64_t since;unsigned long long low,high;int consumed=0;if(sscanf(query,"@%llu:%llu%n",&low,&high,&consumed)==2 && !query[consumed]) { since=(uint64_t)low;r=vault_news_in(store,&p,id,since,scope,entries,&count,&next);if(r==VAULT_OK) { size_t kept=0;for(size_t i=0;i<count;i++)if(entries[i].created<=(uint64_t)high)entries[kept++]=entries[i];count=kept; } }else r=date_filter(query,&since)?vault_news_in(store,&p,id,since,scope,entries,&count,&next):VAULT_INVALID; }
        else r=vault_search(store,&p,id,op==VAULT_CMD_SEARCH?query:"",op==VAULT_CMD_IN?(!strcmp(argument,"-")?"":argument):scope,entries,&count,&next);
        if(r==VAULT_OK) {
            size_t pos=(size_t)(op==VAULT_CMD_LIST?snprintf(out,size,"FILES\r\n"):snprintf(out,size,"%s %s\r\n",op==VAULT_CMD_NEWS?"FILENEWS":op==VAULT_CMD_SEARCH?"FILESEARCH":"FILESIN",op==VAULT_CMD_NEWS && query[0]=='@'?"CHECK":argument));
            for(size_t i=0;i<count;i++) { pos+=(size_t)snprintf(out+pos,size-pos,"FILEITEM %u %u %s\r\n",entries[i].id,entries[i].size,entries[i].name);
                pos+=(size_t)snprintf(out+pos,size-pos,"FILEDETAIL %u %s %llu %s\r\n",entries[i].id,entries[i].ascii?"ASCII":"BINARY",(unsigned long long)downloads_count(entries[i].id),entries[i].description); }
            snprintf(out+pos,size-pos,"END NEXT %u\r\n",next);return;
        }
    } else if(op==VAULT_CMD_INFO) {
        r=vault_info(store,&p,id,&e);
        if(r==VAULT_OK) { char hash[65],protocol[12];if(vault_protocol(store,&p,id,protocol)!=VAULT_OK) { snprintf(out,size,"ERR vault storage\r\n");return; }vault_hex(e.sha256,hash);snprintf(out,size,"FILE %u %u %s\r\nNAME %s\r\nDESCRIPTION %s\r\nPUBLISHER %s\r\nCATEGORY %s\r\nASCII %s\r\nPROTOCOL %s\r\nEND FILE\r\n",e.id,e.size,hash,e.name,e.description,e.publisher,*e.category?e.category:"-",e.ascii?"ON":"OFF",protocol);return; }
    } else if(op==VAULT_CMD_HIDE) {
        r=vault_hide(store,&p,id);
        if(r==VAULT_OK) { snprintf(out,size,"FILE HIDDEN %u\r\n",id);return; }
    }
    if(op==VAULT_CMD_DESCRIPTION || op==VAULT_CMD_RESTORE) {
        r=op==VAULT_CMD_DESCRIPTION?vault_description(store,&p,id,argument):vault_restore(store,&p,id);
        if(r==VAULT_OK) { snprintf(out,size,"FILE %s %u\r\n",op==VAULT_CMD_DESCRIPTION?"EDITED":"RESTORED",id);return; }
    } else if(op==VAULT_CMD_CATEGORY) {
        r=vault_category(store,&p,id,!strcmp(argument,"-")?"":argument);
        if(r==VAULT_OK) { snprintf(out,size,"FILE CATEGORY %u\r\n",id);return; }
    } else if(op==VAULT_CMD_BOARDS) {
        VaultBoard rows[8];size_t count;uint32_t next;r=vault_boards(store,&p,id,rows,&count,&next);
        if(r==VAULT_OK) {
            size_t pos=(size_t)snprintf(out,size,"FILEBOARDS\r\n");
            for(size_t i=0;i<count;i++)pos+=(size_t)snprintf(out+pos,size-pos,"VBOARD %d %s%s%s\r\n",rows[i].can_upload,*rows[i].name?rows[i].name:"-",*rows[i].display?" | ":"",rows[i].display);
            snprintf(out+pos,size-pos,"END NEXT %u\r\n",next);return;
        }
    } else if(op==VAULT_CMD_CATEGORIES) {
        VaultCategory rows[8];size_t count;uint32_t next;r=vault_categories(store,&p,id,rows,&count,&next);
        if(r==VAULT_OK) {
            size_t pos=(size_t)snprintf(out,size,"FILECATEGORIES\r\n");
            for(size_t i=0;i<count;i++)pos+=(size_t)snprintf(out+pos,size-pos,"VCATEGORY %u %s\r\n",rows[i].count,*rows[i].name?rows[i].name:"-");
            snprintf(out+pos,size-pos,"END NEXT %u\r\n",next);return;
        }
    } else if(op==VAULT_CMD_AUDIT) {
        VaultEvent rows[4];size_t count;uint32_t next;r=vault_audit(store,&p,id,rows,&count,&next);
        if(r==VAULT_OK) {
            size_t pos=(size_t)snprintf(out,size,"FILEAUDIT\r\n");
            for(size_t i=0;i<count;i++) {
                char stamp[40]="unknown";time_t at=(time_t)rows[i].at;struct tm value;struct tm *utc=rows[i].at?utc_time(&at,&value):NULL;
                if(utc)strftime(stamp,sizeof(stamp),date_pattern(),utc);
                pos+=(size_t)snprintf(out+pos,size-pos,"VEVENT %u %u %s %c\t%s\t%s\t%s\r\n",rows[i].sequence,rows[i].file,stamp,rows[i].action,rows[i].actor,rows[i].action!='E' && *rows[i].before?rows[i].before:"-",rows[i].action!='E' && *rows[i].after?rows[i].after:"-");
            }
            snprintf(out+pos,size-pos,"END NEXT %u\r\n",next);return;
        }
    }
    const char *message=r==VAULT_DENIED?"ERR vault denied":r==VAULT_NOT_FOUND?"ERR no file":r==VAULT_BUSY?"ERR vault busy":r==VAULT_INVALID?"ERR vault syntax":r==VAULT_LIMIT?"ERR vault limit":"ERR vault storage";
    snprintf(out,size,"%s\r\n",message);
}

void vault_bridge_transfer(IdentityProvider identity,uint32_t user,uint32_t epoch,const char *line,unsigned protocol,char *out,size_t size) {
    if(!store) { snprintf(out,size,"ERR vault disabled\r\n");return; }
    Principal p={0};
    if(!identity.resolve || !identity.resolve(identity.context,user,epoch,&p)) { snprintf(out,size,"ERR login required\r\n");return; }
    const char *protocols[]={"LINE","ZMODEM","XMODEM","YMODEM","YMODEM-G","MLINK","TEXT","XMODEM-SUM","XMODEM-1K"};if(protocol>8)protocol=0;
    if(downloads_command(store,&p,line,protocols[protocol],out,size))return;
    unsigned id=0;int consumed=0;
    if(sscanf(line,"FILEFINISH %u%n",&id,&consumed)==1 && !line[consumed]) {
        VaultResult logged=vault_upload_protocol(store,&p,id,protocols[protocol]);
        if(logged!=VAULT_OK) { snprintf(out,size,"ERR vault protocol log %s\r\n",vault_result_name(logged));return; }
    }
    transfer_line_execute(vault_transfer_backend(store),&p,line,out,size);
    if(sscanf(out,"UPLOAD %u",&id)==1) {
        VaultResult logged=vault_upload_protocol(store,&p,id,protocols[protocol]);
        if(logged!=VAULT_OK)snprintf(out,size,"ERR vault protocol log %s reservation %u\r\n",vault_result_name(logged),id);
    }
}

void vault_bridge_guard(int (*guard)(void *),void *context) { vault_set_guard(store,guard,context);downloads_guard(guard,context); }
static void put_big(unsigned char *p,uint32_t n) { for(int i=3;i>=0;i--) { p[i]=(unsigned char)n;n>>=8; } }
void vault_bridge_binary(IdentityProvider identity,const BoardRequest *q,BoardResult *r) {
    if(!store) { strcpy(r->text,"ERR vault disabled\r\n");return; }
    Principal p={0};
    if(!identity.resolve || !identity.resolve(identity.context,q->user,q->user_epoch,&p)) { strcpy(r->text,"ERR login required\r\n");return; }
    TransferBackend backend=vault_transfer_backend(store);TransferRequest request={0};TransferReply reply={0};
    request.operation=q->action=='R'?TRANSFER_READ:TRANSFER_WRITE;request.id=q->number;request.offset=q->cursor;
    if(q->scope<0 || q->scope>VAULT_CHUNK) { strcpy(r->text,"ERR vault syntax\r\n");return; }
    request.count=(size_t)q->scope;memcpy(request.bytes,q->body,request.count);
    VaultResult result=backend.execute(backend.context,&p,&request,&reply);
    if(result!=VAULT_OK) { snprintf(r->text,sizeof(r->text),"%s\r\n",result==VAULT_DENIED?"ERR vault denied":result==VAULT_BUSY?"ERR vault busy":result==VAULT_NOT_FOUND?"ERR no file":result==VAULT_INVALID?"ERR vault syntax":"ERR vault storage");return; }
    if(request.operation==TRANSFER_READ) {
        unsigned char *data=(unsigned char *)r->text;data[0]='D';put_big(data+1,q->number);put_big(data+5,q->cursor);
        memcpy(data+9,reply.bytes,reply.count);r->binary_size=(unsigned)(9+reply.count);
    } else snprintf(r->text,sizeof(r->text),"RECEIVED %u %u\r\n",q->number,reply.received);
}
