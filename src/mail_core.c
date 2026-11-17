#define _POSIX_C_SOURCE 200809L
#include "mail_core.h"
#include "mail_repository.h"
#include "mailbox.h"
#include "mail_files.h"
#include "members.h"
#include "moderation.h"
#include "app_limits.h"
#include "runtime_limits.h"
#include "store_limits.h"
#include <string.h>

static MailOutcome authorize_sender(const MailActor *actor,int guest) {
    if(members_broken())return MAIL_CORE_MEMBER_STORAGE;
    if(moderation_broken())return MAIL_CORE_MODERATION_STORAGE;
    if(!actor || (!actor->user && !guest))return MAIL_CORE_LOGIN_REQUIRED;
    if(!actor->user) { if(actor->epoch)return MAIL_CORE_REVOKED; }
    else if(actor->user>MEMBER_MAX || !members_name(actor->user) ||
            moderation_suspended(actor->user) || moderation_epoch(actor->user)!=actor->epoch)
        return MAIL_CORE_REVOKED;
    if(!mail_repository_enabled())return MAIL_CORE_DISABLED;
    return mailbox_broken()?MAIL_CORE_STORAGE:MAIL_CORE_OK;
}
static MailOutcome authorize(const MailActor *actor) { return authorize_sender(actor,0); }
static int bounded(const char *text,size_t maximum) {
    if(!text)return 0;
    size_t n=0;while(n<=maximum && text[n])n++;
    return n<=maximum;
}
static int copy(char *out,size_t capacity,const char *text) {
    if(!capacity || !bounded(text,capacity-1))return 0;
    memcpy(out,text,strlen(text)+1);return 1;
}
static MailOutcome store_failure(void) {
    return mailbox_broken()?MAIL_CORE_STORAGE:MAIL_CORE_UNAVAILABLE;
}
static int recipient(uint32_t id) {
    return id && id<=MEMBER_MAX && members_name(id) && !moderation_suspended(id);
}
static int reply_matches(uint32_t user,uint32_t id,uint32_t to) {
    if(!id)return 1;
    const MailMessage *m=mailbox_get(id,user);
    return m && m->recipient==user && m->sender==to;
}
MailOutcome mail_core_prepare(const MailActor *actor,const char *login,uint32_t reply,
                              const char *title,MailCompose *out) {
    if(!out)return MAIL_CORE_UNAVAILABLE;
    memset(out,0,sizeof(*out));
    MailOutcome result=authorize(actor);if(result!=MAIL_CORE_OK)return result;
    if(!bounded(title,80) || (!reply && (!bounded(login,32) || !*login)))return MAIL_CORE_UNAVAILABLE;
    uint32_t to=0,parent=0;char subject[81]={0};
    if(reply) {
        const MailMessage *m=mailbox_get(reply,actor->user);
        if(!m || m->recipient!=actor->user)return store_failure();
        to=m->sender;parent=m->id;
        if(!copy(subject,sizeof(subject),*title?title:m->title))return MAIL_CORE_STORAGE;
    } else { to=members_find(login);memcpy(subject,title,strlen(title)+1); }
    if(!recipient(to))return MAIL_CORE_UNAVAILABLE;
    out->to=to;out->parent=parent;memcpy(out->title,subject,sizeof(subject));return MAIL_CORE_OK;
}
MailOutcome mail_core_draft_get(const MailActor *actor,int resume,MailDraft *out,
                                char *body,size_t capacity) {
    if(body && capacity)memset(body,0,capacity);
    if(!out)return MAIL_CORE_UNAVAILABLE;
    memset(out,0,sizeof(*out));
    MailOutcome result=authorize(actor);if(result!=MAIL_CORE_OK)return result;
    if((resume!=0 && resume!=1) || (resume && (!body || !capacity)))return MAIL_CORE_UNAVAILABLE;
    const MailMessage *stored=mailbox_draft(actor->user);
    if(!stored)return store_failure();
    /* mailbox_get/body may reuse storage caches; retain only this local copy. */
    MailMessage draft=*stored;
    if(draft.attachments.count>MAIL_ATTACH_MAX)return MAIL_CORE_STORAGE;
    if(resume && (!recipient(draft.recipient) || !reply_matches(actor->user,draft.parent,draft.recipient)))return store_failure();
    if(!copy(out->title,sizeof(out->title),draft.title) ||
       !copy(out->recipient,sizeof(out->recipient),members_name(draft.recipient)))result=MAIL_CORE_STORAGE;
    if(result==MAIL_CORE_OK && resume) {
        const char *value=mailbox_body(&draft);
        if(mailbox_broken())result=MAIL_CORE_STORAGE;
        else if(!copy(body,capacity,value))result=MAIL_CORE_UNAVAILABLE;
    }
    if(result!=MAIL_CORE_OK) {
        memset(out,0,sizeof(*out));if(body && capacity)memset(body,0,capacity);return result;
    }
    out->revision=draft.id;out->to=draft.recipient;out->parent=draft.parent;
    out->attachments=draft.attachments;return MAIL_CORE_OK;
}
static MailOutcome write_check(const MailActor *actor,const MailWrite *input) {
    MailOutcome result=authorize(actor);if(result!=MAIL_CORE_OK)return result;
    if(!input || !bounded(input->title,80) || !input->body || input->attachments.count>MAIL_ATTACH_MAX)
        return MAIL_CORE_UNAVAILABLE;
    if(!bounded(input->body,(size_t)ln_limit("BODY",CONTENT_BODY)))return MAIL_CORE_BODY_LIMIT;
    for(unsigned i=0;i<input->attachments.count;i++)
        if(!mail_files_owned(actor->user,input->attachments.ids[i]))return MAIL_CORE_UNAVAILABLE;
    return MAIL_CORE_OK;
}
MailOutcome mail_core_draft_save(const MailActor *actor,const MailWrite *input,uint32_t *revision) {
    if(!revision)return MAIL_CORE_UNAVAILABLE;
    *revision=0;
    MailOutcome result=write_check(actor,input);if(result!=MAIL_CORE_OK)return result;
    uint32_t saved=0;
    if(!mailbox_save_draft(actor->user,input->to,input->parent,input->title,input->body,
                           &input->attachments,input->revision,&saved))return store_failure();
    mail_files_cleanup();*revision=saved;return MAIL_CORE_OK;
}
MailOutcome mail_core_draft_delete(const MailActor *actor,uint32_t revision) {
    MailOutcome result=authorize(actor);if(result!=MAIL_CORE_OK)return result;
    if(!mailbox_delete_draft(actor->user,revision))return store_failure();
    mail_files_cleanup();return MAIL_CORE_OK;
}
MailOutcome mail_core_send(const MailActor *actor,const MailWrite *input,uint32_t *id) {
    if(!id)return MAIL_CORE_UNAVAILABLE;
    *id=0;
    MailOutcome result=write_check(actor,input);if(result!=MAIL_CORE_OK)return result;
    if(!recipient(input->to))return MAIL_CORE_UNAVAILABLE;
    uint32_t saved=0;
    if(!mailbox_send(actor->user,input->to,input->parent,input->title,input->body,
                     &input->attachments,input->revision,&saved))return store_failure();
    mail_files_cleanup();*id=saved;return MAIL_CORE_OK;
}
MailOutcome mail_core_delete(const MailActor *actor,uint32_t id) {
    MailOutcome result=authorize(actor);if(result!=MAIL_CORE_OK)return result;
    if(!mailbox_get(id,actor->user))return store_failure();
    if(!mailbox_remove(id,actor->user))return MAIL_CORE_STORAGE;
    mail_files_cleanup();return MAIL_CORE_OK;
}
MailOutcome mail_core_attachment_select(const MailActor *actor,const MailAttachments *current,
                                        uint32_t id,int detach,MailAttachments *out) {
    if(!out)return MAIL_CORE_UNAVAILABLE;
    memset(out,0,sizeof(*out));
    MailOutcome result=authorize(actor);if(result!=MAIL_CORE_OK)return result;
    if(!current || current->count>MAIL_ATTACH_MAX || (detach!=0 && detach!=1))return MAIL_CORE_UNAVAILABLE;
    if(!detach && id && !mail_files_owned(actor->user,id))return MAIL_CORE_UNAVAILABLE;
    MailAttachments selected=*current;
    if(detach) {
        unsigned n=0;for(unsigned i=0;i<selected.count;i++)if(selected.ids[i]!=id)selected.ids[n++]=selected.ids[i];
        selected.count=n;
    } else if(!id)memset(&selected,0,sizeof(selected));
    else if(!mail_has(&selected,id)) {
        if(selected.count==MAIL_ATTACH_MAX)return MAIL_CORE_UNAVAILABLE;
        selected.ids[selected.count++]=id;
    }
    *out=selected;return MAIL_CORE_OK;
}
MailOutcome mail_core_attachment_drop(const MailActor *actor,uint32_t id) {
    MailOutcome result=authorize(actor);if(result!=MAIL_CORE_OK)return result;
    return mail_files_drop(actor->user,id)?MAIL_CORE_OK:MAIL_CORE_UNAVAILABLE;
}


static MailOutcome summary(const MailMessage *m,MailSummary *out) {
    if(!copy(out->from,sizeof(out->from),m->sender?members_name(m->sender):"guest") ||
       !copy(out->to,sizeof(out->to),members_name(m->recipient)) ||
       !copy(out->title,sizeof(out->title),m->title))return MAIL_CORE_STORAGE;
    out->id=m->id;out->sender=m->sender;out->recipient=m->recipient;
    out->parent=m->parent;out->time=m->time;out->read=m->read;
    return MAIL_CORE_OK;
}
static int search_text(const char *text) {
    if(!bounded(text,80) || !*text)return 0;
    for(const unsigned char *p=(const unsigned char *)text;*p;p++)if(*p<32 || *p==127)return 0;
    return 1;
}
MailOutcome mail_core_list(const MailActor *actor,const MailQuery *query,MailPage *out) {
    if(!out)return MAIL_CORE_UNAVAILABLE;
    memset(out,0,sizeof(*out));
    MailOutcome result=authorize(actor);if(result!=MAIL_CORE_OK)return result;
    if(!query || query->mode<MAIL_LIST_ALL || query->mode>MAIL_LIST_NEWS || query->cursor>CONTENT_ID_MAX ||
       (query->mode==MAIL_LIST_SEARCH && !search_text(query->query)))return MAIL_CORE_UNAVAILABLE;
    uint32_t total=mailbox_count();if(total>CONTENT_ID_MAX)return MAIL_CORE_STORAGE;
    out->unread=mailbox_unread(actor->user);
    for(uint32_t i=query->cursor+1;i<=total;i++) {
        if(query->mode==MAIL_LIST_SEARCH) {
            uint32_t candidate=mailbox_search_next(i,query->query);
            if(!candidate)break;
            if(candidate<i || candidate>total) { result=MAIL_CORE_STORAGE;break; }
            i=candidate;
        }
        const MailMessage *stored=mailbox_get(i,actor->user);if(!stored)continue;
        MailMessage m=*stored;
        if((query->mode==MAIL_LIST_SENT && m.sender!=actor->user) ||
           (query->mode==MAIL_LIST_UNREAD && (m.recipient!=actor->user || m.read)) ||
           (query->mode==MAIL_LIST_INBOX && m.recipient!=actor->user))continue;
        if(query->mode==MAIL_LIST_NEWS && (m.recipient!=actor->user || m.time<query->since ||
           (query->until && m.time>query->until)))continue;
        if(query->mode==MAIL_LIST_SEARCH) {
            const char *body=mailbox_body(&m);
            if(mailbox_broken() || !body) { result=MAIL_CORE_BODY_STORAGE;break; }
            if(!strstr(body,query->query) && !strstr(m.title,query->query))continue;
        }
        if(out->count==8) { out->next=i-1;break; }
        result=summary(&m,&out->rows[out->count]);if(result!=MAIL_CORE_OK)break;
        out->count++;
    }
    if(result==MAIL_CORE_OK && mailbox_broken())result=MAIL_CORE_STORAGE;
    if(result!=MAIL_CORE_OK)memset(out,0,sizeof(*out));
    return result;
}
MailOutcome mail_core_read(const MailActor *actor,MailReadMode mode,uint32_t start,int descending,
                          MailRead *out,char *body,size_t capacity) {
    if(body && capacity)memset(body,0,capacity);
    if(!out)return MAIL_CORE_UNAVAILABLE;
    memset(out,0,sizeof(*out));
    MailOutcome result=authorize(actor);if(result!=MAIL_CORE_OK)return result;
    if(mode<MAIL_READ_ID || mode>MAIL_READ_SEQUENCE || (descending!=0 && descending!=1) ||
       !body || !capacity)return MAIL_CORE_UNAVAILABLE;
    uint32_t total=mailbox_count(),id=start;
    if(total>CONTENT_ID_MAX)return MAIL_CORE_STORAGE;
    if(mode==MAIL_READ_UNREAD || mode==MAIL_READ_LATEST) {
        id=0;
        for(uint32_t i=1;i<=total;i++) {
            const MailMessage *m=mailbox_get(i,actor->user);if(!m)continue;
            if(mode==MAIL_READ_LATEST)id=i;
            else if(!m->read && m->recipient==actor->user) { id=i;break; }
        }
    } else if(mode==MAIL_READ_SEQUENCE) {
        uint32_t i=start;if(descending && i>total)i=total;
        id=0;
        for(;i && i<=total;i=descending?i-1:i+1)
            if(mailbox_get(i,actor->user)) { id=i;break; }
        if(!id) {
            if(mailbox_broken())return MAIL_CORE_STORAGE;
            out->sequence=1;return MAIL_CORE_OK;
        }
    }
    const MailMessage *stored=mailbox_get(id,actor->user);if(!stored)return store_failure();
    MailMessage m=*stored;
    if(m.attachments.count>MAIL_ATTACH_MAX || m.body_size>CONTENT_BODY)return MAIL_CORE_STORAGE;
    if((size_t)m.body_size>=capacity)return MAIL_CORE_UNAVAILABLE;
    if(!mailbox_mark(id,actor->user))return MAIL_CORE_STORAGE;
    /* Marking may invalidate cached metadata; do not reuse the borrowed row. */
    stored=mailbox_get(id,actor->user);
    if(!stored)return MAIL_CORE_STORAGE;
    m=*stored;
    result=summary(&m,&out->message);
    if(result==MAIL_CORE_OK) {
        const char *value=mailbox_body(&m);
        if(mailbox_broken() || !value)result=MAIL_CORE_BODY_STORAGE;
        else if(!copy(body,capacity,value))result=MAIL_CORE_STORAGE;
    }
    if(result!=MAIL_CORE_OK) {
        memset(out,0,sizeof(*out));memset(body,0,capacity);return result;
    }
    out->attachments=m.attachments;
    if(mode==MAIL_READ_SEQUENCE) { out->sequence=1;out->next=descending?id-1:id<total?id+1:0; }
    return MAIL_CORE_OK;
}

MailOutcome mail_core_files(const MailActor *actor,uint32_t cursor,MailFilePage *out) {
    if(!out)return MAIL_CORE_UNAVAILABLE;
    memset(out,0,sizeof(*out));
    MailOutcome result=authorize(actor);if(result!=MAIL_CORE_OK)return result;
    MailFilePage page={0};
    if(!mail_files_page(actor->user,cursor,&page))return MAIL_CORE_STORAGE;
    if(page.count>8)return MAIL_CORE_STORAGE;
    for(unsigned i=0;i<page.count;i++)if(!bounded(page.rows[i].name,sizeof(page.rows[i].name)-1))return MAIL_CORE_STORAGE;
    *out=page;return MAIL_CORE_OK;
}
MailOutcome mail_core_attachment_info(const MailActor *actor,uint32_t id,MailFileInfo *out) {
    if(!out)return MAIL_CORE_UNAVAILABLE;
    memset(out,0,sizeof(*out));
    MailOutcome result=authorize(actor);if(result!=MAIL_CORE_OK)return result;
    MailFileInfo info={0};
    if(!id || !mail_files_info(actor->user,id,&info))return store_failure();
    if(info.id!=id || !bounded(info.name,sizeof(info.name)-1))return MAIL_CORE_STORAGE;
    *out=info;return MAIL_CORE_OK;
}
MailOutcome mail_core_attachment_batch(const MailActor *actor,uint32_t id,MailAttachments *out) {
    if(!out)return MAIL_CORE_UNAVAILABLE;
    memset(out,0,sizeof(*out));
    MailOutcome result=authorize(actor);if(result!=MAIL_CORE_OK)return result;
    const MailMessage *m=mailbox_get(id,actor->user);
    if(!m || !m->attachments.count)return store_failure();
    if(m->attachments.count>MAIL_ATTACH_MAX)return MAIL_CORE_STORAGE;
    *out=m->attachments;return MAIL_CORE_OK;
}
MailOutcome mail_core_import(const MailActor *actor,uint32_t id) {
    MailOutcome result=authorize(actor);if(result!=MAIL_CORE_OK)return result;
    const MailMessage *stored=mailbox_get(id,actor->user);
    if(!stored)return store_failure();
    MailMessage message=*stored;
    const char *body=mailbox_body(&message);
    if(mailbox_broken() || !body || !bounded(body,CONTENT_BODY))return MAIL_CORE_BODY_STORAGE;
    return mail_repository_buffer_save(actor->user,body)?MAIL_CORE_OK:MAIL_CORE_BUFFER_STORAGE;
}

MailOutcome mail_core_sysop_send(const MailActor *actor,const char *title,const char *body,MailDelivery *out) {
    if(!out)return MAIL_CORE_UNAVAILABLE;
    memset(out,0,sizeof(*out));
    MailOutcome result=authorize_sender(actor,1);if(result!=MAIL_CORE_OK)return result;
    if(!bounded(title,80) || !*title || !body || !*body)return MAIL_CORE_UNAVAILABLE;
    if(!bounded(body,(size_t)ln_limit("BODY",CONTENT_BODY)))return MAIL_CORE_BODY_LIMIT;
    uint32_t count=members_count(),to=0,id=0;
    if(count>MEMBER_MAX)return MAIL_CORE_MEMBER_STORAGE;
    for(uint32_t i=1;i<=count;i++)if(moderation_admin(i) && recipient(i)) { to=i;break; }
    if(!to)return MAIL_CORE_SYSOP_UNAVAILABLE;
    if(!mailbox_send(actor->user,to,0,title,body,NULL,0,&id))return store_failure();
    out->id=id;out->recipient=to;return MAIL_CORE_OK;
}
MailOutcome mail_core_notice(const MailActor *actor,MailNotice *out) {
    if(!out)return MAIL_CORE_UNAVAILABLE;
    memset(out,0,sizeof(*out));
    MailOutcome result=authorize(actor);if(result!=MAIL_CORE_OK)return result;
    unsigned unread=mailbox_unread(actor->user);
    if(mailbox_broken())return MAIL_CORE_STORAGE;
    const MailMessage *draft=mailbox_draft(actor->user);
    if(mailbox_broken())return MAIL_CORE_STORAGE;
    out->unread=unread;out->draft_revision=draft?draft->id:0;return MAIL_CORE_OK;
}
MailOutcome mail_core_transfer(const MailActor *actor,const TransferRequest *q,TransferReply *out,VaultResult *status) {
    if(out)memset(out,0,sizeof(*out));
    if(status)*status=VAULT_DENIED;
    if(!out || !status)return MAIL_CORE_UNAVAILABLE;
    MailOutcome result=authorize(actor);if(result!=MAIL_CORE_OK)return result;
    *status=VAULT_INVALID;
    if(!q || q->operation<TRANSFER_INFO || q->operation>TRANSFER_CHECK)return MAIL_CORE_OK;
    if(q->operation==TRANSFER_CHECK || q->operation==TRANSFER_START) {
        if(!bounded(q->category,sizeof(q->category)-1) || *q->category)return MAIL_CORE_OK;
        if(q->operation==TRANSFER_START && (q->size>VAULT_MAX_BYTES ||
            !bounded(q->name,100) || !*q->name || !bounded(q->description,240)))return MAIL_CORE_OK;
    } else {
        if(!q->id || q->id>VAULT_ID_MAX)return MAIL_CORE_OK;
        if((q->operation==TRANSFER_READ || q->operation==TRANSFER_WRITE) &&
           (q->offset>VAULT_MAX_BYTES || q->count>VAULT_BLOCK ||
            (q->operation==TRANSFER_WRITE && !q->count)))return MAIL_CORE_OK;
    }
    *status=mail_files_execute(actor->user,q,out);
    if(*status==VAULT_OK && q->operation==TRANSFER_READ && out->count>VAULT_BLOCK)*status=VAULT_STORAGE;
    if(*status!=VAULT_OK)memset(out,0,sizeof(*out));
    return MAIL_CORE_OK;
}
