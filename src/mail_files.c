/* Private blob storage reuses the protocol-neutral vault engine, never its
 * public directory or account adapter. SYSOP has no implicit mail read access. */
#include "mail_files.h"
#include "mailbox.h"
#include "vault.h"
#include "store_limits.h"
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
VaultResult mail_files_execute(uint32_t user,const TransferRequest *q,TransferReply *r) {
    if(!r)return VAULT_INVALID;
    memset(r,0,sizeof(*r));
    if(!user || !q)return VAULT_DENIED;
    if(!ready())return VAULT_STORAGE;
    if(q->operation==TRANSFER_CHECK)return *q->category?VAULT_INVALID:VAULT_OK;
    if(q->operation==TRANSFER_START && *q->category)return VAULT_INVALID;
    if(q->operation==TRANSFER_READ || q->operation==TRANSFER_INFO) {
        VaultEntry e;VaultResult result=entry(user,q->id,&e,0);if(result!=VAULT_OK)return result;
    }
    Principal p=member(user);TransferBackend backend=vault_transfer_backend(files);
    VaultResult result=backend.execute(backend.context,&p,q,r);
    if(result!=VAULT_OK)memset(r,0,sizeof(*r));
    return result;
}
int mail_files_info(uint32_t user,uint32_t id,MailFileInfo *out) {
    if(!out)return 0;
    memset(out,0,sizeof(*out));VaultEntry e;
    if(entry(user,id,&e,0)!=VAULT_OK)return 0;
    out->id=e.id;out->size=e.size;memcpy(out->name,e.name,sizeof(out->name));return 1;
}
int mail_files_page(uint32_t user,uint32_t cursor,MailFilePage *out) {
    if(!out)return 0;
    memset(out,0,sizeof(*out));
    if(!user || cursor>VAULT_ID_MAX || !ready())return 0;
    Principal p=member(user);MailFilePage page={0};uint32_t id=cursor?cursor:1;
    for(;id<=vault_last_id(files) && page.count<8;id++) {
        VaultUpload upload;VaultResult r=vault_upload_status(files,&p,id,&upload);
        if(r==VAULT_STORAGE) return 0;
        if(r!=VAULT_OK || upload.state==VAULT_UPLOAD_CANCELLED)continue;
        MailFileInfo item={.id=id,.size=upload.size,.active=upload.state==VAULT_UPLOAD_ACTIVE};
        strcpy(item.name,"-");
        if(upload.state==VAULT_UPLOAD_COMMITTED) {
            VaultEntry e;r=entry(user,id,&e,1);
            if(r==VAULT_STORAGE)return 0;
            if(r!=VAULT_OK)continue;
            memcpy(item.name,e.name,sizeof(item.name));
        }
        page.rows[page.count++]=item;
    }
    page.next=id<=vault_last_id(files)?id:0;*out=page;return 1;
}
