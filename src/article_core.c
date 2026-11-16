#include "article_core.h"
#include "article_repository.h"
#include "members.h"
#include "catalog.h"
#include "moderation.h"
#include "board_policy.h"
#include "extras.h"
#include "content_limits.h"
#include <string.h>

static ArticleOutcome health(void) {
    if(members_broken())return ARTICLE_MEMBER_STORAGE;
    if(moderation_broken())return ARTICLE_MODERATION_STORAGE;
    if(policy_broken())return ARTICLE_POLICY_STORAGE;
    if(extras_broken())return ARTICLE_EXTRAS_STORAGE;
    return ARTICLE_OK;
}

static ArticleOutcome identity(const ArticleActor *actor,int guest_allowed) {
    if(!actor || !actor->user)return guest_allowed?ARTICLE_OK:ARTICLE_LOGIN_REQUIRED;
    const char *name=members_name(actor->user);
    if(!name || moderation_suspended(actor->user) ||
       moderation_epoch(actor->user)!=actor->epoch)return ARTICLE_REVOKED;
    return ARTICLE_OK;
}

static ArticleOutcome authorize(const ArticleActor *actor,uint32_t board,uint32_t id) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,0);
    if(result!=ARTICLE_OK)return result;
    const char *name=members_name(actor->user);
    ArticleMetadata article;
    if(!article_repository_find(id,&article) || article.board!=board ||
       !catalog_read(board,actor->user))return ARTICLE_NOT_FOUND;
    int owner=*article.author && strcmp(article.author,"guest") && !strcmp(article.author,name);
    if(!owner && !catalog_manage(board,actor->user))return ARTICLE_OWNER_REQUIRED;
    return ARTICLE_OK;
}

static ArticleOutcome withdraw(const ArticleActor *actor,uint32_t board,uint32_t id,int confirmed) {
    ArticleOutcome result=authorize(actor,board,id);
    if(result!=ARTICLE_OK)return result;
    int imported=article_repository_imported(id);
    if(imported<0)return ARTICLE_LINK_STORAGE;
    if(imported)return ARTICLE_IMPORTED;
    if(!confirmed)return ARTICLE_CONFIRM;
    return extras_set('D',id,actor->user,"")?ARTICLE_OK:ARTICLE_EXTRAS_STORAGE;
}

static ArticleOutcome retitle(const ArticleActor *actor,uint32_t board,uint32_t id,const char *title) {
    ArticleOutcome result=authorize(actor,board,id);
    if(result!=ARTICLE_OK)return result;
    int hidden=article_repository_unavailable(id);
    if(hidden<0)return ARTICLE_STORAGE;
    if(hidden)return ARTICLE_HIDDEN;
    if(!catalog_write(board,actor->user))return ARTICLE_POSTING_DENIED;
    if(!title || !*title)return ARTICLE_INVALID_TITLE;
    size_t n=0;
    for(const unsigned char *p=(const unsigned char *)title;*p;p++)
        if(++n>80 || *p<32 || *p==127)return ARTICLE_INVALID_TITLE;
    if(!extras_set('T',id,actor->user,title))return ARTICLE_EXTRAS_STORAGE;
    article_repository_title_changed(id,title);
    return ARTICLE_OK;
}

ArticleOutcome article_withdraw(const ArticleActor *actor,uint32_t board,uint32_t id,int confirmed) {
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=withdraw(actor,board,id,confirmed);
    policy_context(NULL);
    return result;
}
ArticleOutcome article_retitle(const ArticleActor *actor,uint32_t board,uint32_t id,const char *title) {
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=retitle(actor,board,id,title);
    policy_context(NULL);
    return result;
}

static ArticleOutcome read_article(const ArticleActor *actor,uint32_t id,int inspect,
                                   ArticleView *out,char *body,size_t capacity) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,1);
    if(result!=ARTICLE_OK)return result;
    uint32_t user=actor?actor->user:0;
    ArticleMetadata article;
    if(!article_repository_find(id,&article) || !catalog_read(article.board,user))
        return ARTICLE_NOT_FOUND;
    if(inspect && (!user || !catalog_manage(article.board,user)))return ARTICLE_ADMIN_REQUIRED;
    int hidden=article_repository_unavailable(id);
    if(hidden<0)return ARTICLE_STORAGE;
    out->id=id;out->board=article.board;out->parent=article.parent;
    if(extras_removed(id)) { out->state=ARTICLE_WITHDRAWN;return ARTICLE_OK; }
    if(hidden && !inspect) { out->state=ARTICLE_CONCEALED;return ARTICLE_OK; }
    if(article.body_size>CONTENT_BODY)return ARTICLE_STORAGE;
    if(!body || capacity<=article.body_size)return ARTICLE_BUFFER_TOO_SMALL;
    if(!article_repository_body(id,body,capacity))return ARTICLE_STORAGE;
    out->state=ARTICLE_VISIBLE;out->inspected_hidden=hidden;
    out->body_size=article.body_size;
    memcpy(out->author,article.author,sizeof(out->author));
    memcpy(out->title,article.title,sizeof(out->title));
    return ARTICLE_OK;
}

ArticleOutcome article_read(const ArticleActor *actor,uint32_t id,int inspect,
                            ArticleView *out,char *body,size_t capacity) {
    if(body && capacity)memset(body,0,capacity);
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=read_article(actor,id,inspect,out,body,capacity);
    policy_context(NULL);
    if(result!=ARTICLE_OK) {
        memset(out,0,sizeof(*out));
        if(body && capacity)memset(body,0,capacity);
    }
    return result;
}
