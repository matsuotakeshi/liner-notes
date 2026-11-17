#include "mail_core.h"
#include "mail_repository.h"
#include "mailbox.h"
#include "mail_files.h"
#include "members.h"
#include "moderation.h"
#include "app_limits.h"
#include "runtime_limits.h"
#include <string.h>

static MailOutcome authorize(const MailActor *actor) {
    if(members_broken())return MAIL_CORE_MEMBER_STORAGE;
    if(moderation_broken())return MAIL_CORE_MODERATION_STORAGE;
    if(!actor || !actor->user)return MAIL_CORE_LOGIN_REQUIRED;
    if(actor->user>MEMBER_MAX || !members_name(actor->user) ||
       moderation_suspended(actor->user) || moderation_epoch(actor->user)!=actor->epoch)
        return MAIL_CORE_REVOKED;
    if(!mail_repository_enabled())return MAIL_CORE_DISABLED;
    return mailbox_broken()?MAIL_CORE_STORAGE:MAIL_CORE_OK;
}
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
