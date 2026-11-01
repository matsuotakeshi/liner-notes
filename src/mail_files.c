/* Private blob storage reuses the protocol-neutral vault engine, never its
 * public directory or account adapter. SYSOP has no implicit mail read access. */
#include "mail_files.h"
#include "mailbox.h"
#include "vault.h"
#include "store_limits.h"
#include "transfer_line.h"
#include "messages.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static Vault *files;
static unsigned char *reclaimed;static size_t reclaimed_capacity;

static char directory[4096];
static Principal member(uint32_t user) {
    Principal p={0};snprintf(p.subject,sizeof(p.subject),"mail-member:%u",user);
    p.capabilities=CAP_VAULT_READ|CAP_VAULT_UPLOAD;return p;
}
void mail_files_close(void) { vault_close(files);files=NULL;free(reclaimed);reclaimed=NULL;reclaimed_capacity=0; }
void mail_files_config(const char *base) {
    mail_files_close();*directory=0;
    if(base && strlen(base)<3500)snprintf(directory,sizeof(directory),"%s.mailfiles",base);
}
void mail_files_cleanup(void) {
    if(!files || mailbox_broken())return;
    if(!store_reserve((void **)&reclaimed,&reclaimed_capacity,(size_t)mailbox_attachment_last()+1,1))return;
    Principal system={"mail-reclaim",CAP_VAULT_MANAGE,0,0,{0},{0}};
    for(uint32_t id=1;id<=mailbox_attachment_last();id++)if(!reclaimed[id] && mailbox_attachment_used(id) && !mailbox_attachment_referenced(id)) {
        VaultResult result=vault_reclaim(files,&system,id);
        if(result==VAULT_OK || result==VAULT_NOT_FOUND)reclaimed[id]=1;else fprintf(stderr,"mail attachment reclamation deferred: %u\n",id);
    }
}
static int ready(void) {
    if(mailbox_broken() || !*directory)return 0;
    if(files)return 1;
    int known=0;for(uint32_t id=1;id<=mailbox_attachment_last();id++)if(mailbox_attachment_used(id)) { known=1;break; }
    /* Never recreate a missing store whose IDs are already referenced by mail. */
    if((!known && vault_init(directory)!=VAULT_OK) || vault_open(directory,&files)!=VAULT_OK)return 0;
    mail_files_cleanup();return 1;
}
static VaultResult entry(uint32_t user,uint32_t id,VaultEntry *e,int own) {
    if(!user || !ready())return VAULT_STORAGE;
    Principal p=member(user);VaultResult r=vault_info(files,&p,id,e);
    if(r==VAULT_OK && strcmp(e->publisher,p.subject) && (own || !mailbox_attachment_readable(id,user)))return VAULT_NOT_FOUND;
    return r;
}
int mail_files_owned(uint32_t user,uint32_t id) { VaultEntry e;return entry(user,id,&e,1)==VAULT_OK; }
int mail_files_drop(uint32_t user,uint32_t id) {
    if(!user || !id || id>VAULT_ID_MAX || !ready() || mailbox_attachment_referenced(id))return 0;
    Principal p=member(user);VaultResult r=vault_reclaim(files,&p,id);
    if(r==VAULT_NOT_FOUND)r=vault_upload_cancel(files,&p,id);
    return r==VAULT_OK;
}
static VaultResult transfer(void *context,const Principal *p,const TransferRequest *q,TransferReply *r) {
    uint32_t user=*(uint32_t *)context;
    if(!ready())return VAULT_STORAGE;
    if(q->operation==TRANSFER_CHECK)return *q->category?VAULT_INVALID:VAULT_OK;
    if(q->operation==TRANSFER_START && *q->category)return VAULT_INVALID;
    if(q->operation==TRANSFER_READ || q->operation==TRANSFER_INFO) {
        VaultEntry e;VaultResult result=entry(user,q->id,&e,0);if(result!=VAULT_OK)return result;
    }
    TransferBackend backend=vault_transfer_backend(files);return backend.execute(backend.context,p,q,r);
}
void mail_files_transfer(uint32_t user,const char *line,char *out,size_t size) {
    if(!user) { snprintf(out,size,"ERR login required\r\n");return; }
    if(!strncmp(line,"FILE ",5)) {
        char *end;unsigned long id=strtoul(line+5,&end,10);VaultEntry e;
        if(end==line+5 || *end || !id || id>VAULT_ID_MAX || entry(user,(uint32_t)id,&e,0)!=VAULT_OK) { snprintf(out,size,"ERR no file\r\n");return; }
        char hash[65];vault_hex(e.sha256,hash);snprintf(out,size,"FILE %u %u %s\r\nNAME %s\r\nDESCRIPTION %s\r\nEND FILE\r\n",e.id,e.size,hash,e.name,e.description);return;
    }
    Principal p=member(user);TransferBackend backend={&user,transfer};transfer_line_execute(backend,&p,line,out,size);
}
static const char *text(int language,MessageId id,const char *raw) { return language?message_text(language==2?LANGUAGE_EN:LANGUAGE_JA,id):raw; }
size_t mail_files_describe(uint32_t user,uint32_t id,int language,char *out,size_t size) {
    VaultEntry e;
    if(entry(user,id,&e,0)!=VAULT_OK)return (size_t)snprintf(out,size,"%s",text(language,MSG_MAIL_ATTACHMENT_MISSING,"MAIL ATTACHMENT unavailable\r\n"));
    return (size_t)snprintf(out,size,text(language,MSG_MAIL_ATTACHMENT_INFO,"MAIL ATTACHMENT %u SIZE %u NAME %s\r\n"),id,e.size,e.name);
}
void mail_files_list(uint32_t user,uint32_t cursor,int language,char *out,size_t size,uint32_t *next) {
    *next=0;if(!user || cursor>VAULT_ID_MAX || !ready()) { snprintf(out,size,"%s",text(language,MSG_MAIL_STORAGE,"MAIL ERROR storage\r\n"));return; }
    Principal p=member(user);size_t used=(size_t)snprintf(out,size,"%s",text(language,MSG_MAIL_FILES,"MAIL FILES\r\n"));unsigned shown=0;uint32_t id=cursor?cursor:1;
    for(;id<=vault_last_id(files) && shown<8;id++) {
        VaultUpload upload;VaultResult r=vault_upload_status(files,&p,id,&upload);
        if(r!=VAULT_OK || upload.state==VAULT_UPLOAD_CANCELLED)continue;
        VaultEntry e;const char *name="-";
        if(upload.state==VAULT_UPLOAD_COMMITTED) { if(entry(user,id,&e,1)!=VAULT_OK)continue;name=e.name; }
        used+=(size_t)snprintf(out+used,size-used,text(language,MSG_MAIL_FILE_ROW,"MAILFILE %u SIZE %u STATE %s NAME %s\r\n"),id,upload.size,upload.state==VAULT_UPLOAD_ACTIVE?text(language,MSG_MAIL_FILE_ACTIVE,"ACTIVE"):text(language,MSG_MAIL_FILE_READY,"READY"),name);shown++;
    }
    *next=id<=vault_last_id(files)?id:0;snprintf(out+used,size-used,text(language,MSG_MAIL_FILES_END,"END MAILFILES NEXT %u\r\n"),*next);
}
