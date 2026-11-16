#include "article_core.h"
#include "article_repository.h"
#include "members.h"
#include "catalog.h"
#include "moderation.h"
#include "board_policy.h"
#include "extras.h"
#include "content_limits.h"
#include "store_limits.h"
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

static ArticleOutcome moderate(const ArticleActor *actor,uint32_t board,uint32_t id,int hide) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,0);
    if(result!=ARTICLE_OK)return result;
    if(!catalog_manage(board,actor->user))return ARTICLE_ADMIN_REQUIRED;
    ArticleMetadata article;
    if(!article_repository_find(id,&article) || article.board!=board ||
       !catalog_read(board,actor->user))return ARTICLE_NOT_FOUND;
    if(extras_removed(id))return ARTICLE_WITHDRAWN_TARGET;
    if((uint64_t)moderation_count()+extras_audit_count()>=UINT32_MAX)
        return ARTICLE_MODERATION_WRITE;
    return moderation_set(id,board,actor->user,hide)?ARTICLE_OK:ARTICLE_MODERATION_WRITE;
}

static ArticleOutcome local_moderation(const ArticleActor *actor,uint32_t board,uint32_t id,int hide) {
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=moderate(actor,board,id,hide);
    policy_context(NULL);
    return result;
}

ArticleOutcome article_hide(const ArticleActor *actor,uint32_t board,uint32_t id) {
    return local_moderation(actor,board,id,1);
}
ArticleOutcome article_restore(const ArticleActor *actor,uint32_t board,uint32_t id) {
    return local_moderation(actor,board,id,0);
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

static int visible_summary(uint32_t id,uint32_t board,ArticleMetadata *article) {
    if(!article_repository_find(id,article))return -1;
    if(article->board!=board)return 0;
    int hidden=article_repository_unavailable(id);
    return hidden<0?-1:!hidden;
}

static void add_summary(ArticlePage *out,uint32_t id,const ArticleMetadata *article) {
    ArticleSummary *row=&out->rows[out->count++];
    row->id=id;row->board=article->board;row->parent=article->parent;
    memcpy(row->title,article->title,sizeof(row->title));
}

static ArticleOutcome page_articles(const ArticleActor *actor,uint32_t board,uint32_t start,
                                     const char *query,int search,ArticlePage *out) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,1);
    if(result!=ARTICLE_OK)return result;
    if(!catalog_read(board,actor?actor->user:0))return ARTICLE_BOARD_DENIED;
    if(search) {
        if(!query || !*query)return ARTICLE_INVALID_QUERY;
        size_t length=0;
        for(const unsigned char *p=(const unsigned char *)query;*p;p++)
            if(++length>80 || *p<32 || *p==127)return ARTICLE_INVALID_QUERY;
    }
    uint32_t count=article_repository_count(),id=start?start:1;
    if(count>CONTENT_ID_MAX)return ARTICLE_STORAGE;
    if(search && id>count+1)return ARTICLE_INVALID_CURSOR;
    if(!search) {
        for(uint32_t n=1;n<=count;n++) {
            ArticleMetadata article;int visible=visible_summary(n,board,&article);
            if(visible<0)return ARTICLE_STORAGE;
            if(visible)out->total++;
        }
    }
    unsigned scanned=0;
    for(;id<=count;id++) {
        if(search) {
            if(out->count==8 || scanned==256)break;
            uint32_t candidate=article_repository_search_next(id,query);
            if(!candidate) { id=count+1;break; }
            if(candidate<id || candidate>count)return ARTICLE_STORAGE;
            id=candidate;scanned++;
        }
        ArticleMetadata article;int visible=visible_summary(id,board,&article);
        if(visible<0)return ARTICLE_STORAGE;
        if(!visible)continue;
        if(!search && out->count==5)break;
        if(search && !strstr(article.title,query)) {
            int matched=article_repository_body_contains(id,query);
            if(matched<0)return ARTICLE_STORAGE;
            if(!matched)continue;
        }
        add_summary(out,id,&article);
    }
    out->next=id<=count?id:0;
    return ARTICLE_OK;
}

static ArticleOutcome article_page(const ArticleActor *actor,uint32_t board,uint32_t start,
                                    const char *query,int search,ArticlePage *out) {
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=page_articles(actor,board,start,query,search,out);
    policy_context(NULL);
    if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
    return result;
}

ArticleOutcome article_list(const ArticleActor *actor,uint32_t board,uint32_t start,ArticlePage *out) {
    return article_page(actor,board,start,NULL,0,out);
}
ArticleOutcome article_search(const ArticleActor *actor,uint32_t board,uint32_t start,
                              const char *query,ArticlePage *out) {
    return article_page(actor,board,start,query,1,out);
}

static ArticleOutcome tree_articles(const ArticleActor *actor,uint32_t board,uint32_t target,
                                     uint32_t start,ArticleTreePage *out) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,1);
    if(result!=ARTICLE_OK)return result;
    if(!catalog_read(board,actor?actor->user:0))return ARTICLE_BOARD_DENIED;
    uint32_t count=article_repository_count();
    if(count>CONTENT_ID_MAX)return ARTICLE_STORAGE;
    ArticleMetadata article;
    if(!article_repository_find(target,&article) || article.board!=board)return ARTICLE_NOT_FOUND;
    uint32_t root=article.root,id=start?start:root;
    if(!root || root>count)return ARTICLE_STORAGE;
    if(!id || id>count || !article_repository_find(id,&article) || article.root!=root || article.board!=board)
        return ARTICLE_INVALID_CURSOR;
    out->root=root;
    for(uint32_t visited=0;id;id=article_repository_tree_next(id)) {
        if(++visited>count || id>count || !article_repository_find(id,&article) ||
           article.board!=board || article.root!=root)return ARTICLE_STORAGE;
        int hidden=article_repository_unavailable(id);
        if(hidden<0)return ARTICLE_STORAGE;
        if(hidden)continue;
        for(unsigned i=0;i<out->count;i++)if(out->rows[i].summary.id==id)return ARTICLE_STORAGE;
        if(out->count==8) { out->next=id;break; }
        ArticleTreeRow *row=&out->rows[out->count++];
        row->summary.id=id;row->summary.board=board;row->summary.parent=article.parent;
        memcpy(row->summary.title,article.title,sizeof(row->summary.title));row->depth=article.depth;
    }
    return ARTICLE_OK;
}

ArticleOutcome article_tree(const ArticleActor *actor,uint32_t board,uint32_t target,
                            uint32_t start,ArticleTreePage *out) {
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=tree_articles(actor,board,target,start,out);
    policy_context(NULL);
    if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
    return result;
}
