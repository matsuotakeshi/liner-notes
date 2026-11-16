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

static ArticleOutcome navigate_article(const ArticleActor *actor,uint32_t board,uint32_t current,
                                       ArticleDirection direction,ArticleSummary *out) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,1);
    if(result!=ARTICLE_OK)return result;
    if(!catalog_read(board,actor?actor->user:0))return ARTICLE_BOARD_DENIED;
    if(direction!=ARTICLE_PARENT && direction!=ARTICLE_CHILD &&
       direction!=ARTICLE_NEXT && direction!=ARTICLE_PREVIOUS)return ARTICLE_INVALID_QUERY;
    uint32_t count=article_repository_count();
    if(count>CONTENT_ID_MAX)return ARTICLE_STORAGE;
    ArticleMetadata source;
    if(!current || current>count || !article_repository_find(current,&source) ||
       source.board!=board)return ARTICLE_NOT_FOUND;
    uint32_t target=direction==ARTICLE_PARENT?source.parent:direction==ARTICLE_CHILD?source.child:
                    direction==ARTICLE_NEXT?source.next:source.prev;
    uint32_t parent=direction==ARTICLE_CHILD?current:source.parent;
    for(uint32_t visited=0;target;visited++) {
        ArticleMetadata article;
        if(visited>=count || target==current || target>count ||
           !article_repository_find(target,&article) || article.board!=board ||
           (direction!=ARTICLE_PARENT && article.parent!=parent))return ARTICLE_STORAGE;
        int hidden=article_repository_unavailable(target);
        if(hidden<0)return ARTICLE_STORAGE;
        if(!hidden) {
            out->id=target;out->board=board;out->parent=article.parent;
            memcpy(out->title,article.title,sizeof(out->title));return ARTICLE_OK;
        }
        target=direction==ARTICLE_PARENT?article.parent:
               direction==ARTICLE_PREVIOUS?article.prev:article.next;
    }
    return ARTICLE_OK;
}

ArticleOutcome article_navigate(const ArticleActor *actor,uint32_t board,uint32_t current,
                                ArticleDirection direction,ArticleSummary *out) {
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=navigate_article(actor,board,current,direction,out);
    policy_context(NULL);
    if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
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

static ArticleOutcome news_articles(const ArticleActor *actor,uint32_t board,ArticleScope scope,
                                    uint32_t start,uint64_t since,uint64_t until,ArticlePage *out) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,1);
    if(result!=ARTICLE_OK)return result;
    if(scope!=ARTICLE_HERE && scope!=ARTICLE_ALL && scope!=ARTICLE_SUBTREE)
        return ARTICLE_INVALID_QUERY;
    if(start>CONTENT_ID_MAX+1)return ARTICLE_INVALID_CURSOR;
    uint32_t user=actor?actor->user:0;
    if(scope==ARTICLE_HERE && !catalog_read(board,user))return ARTICLE_BOARD_DENIED;
    uint32_t count=article_repository_count(),id=start?start:1;
    if(count>CONTENT_ID_MAX)return ARTICLE_STORAGE;
    unsigned scanned=0;
    for(;id<=count && out->count<8 && scanned<256;id++,scanned++) {
        ArticleMetadata article;
        if(!article_repository_find(id,&article))return ARTICLE_STORAGE;
        if((scope==ARTICLE_HERE && article.board!=board) ||
           (scope==ARTICLE_SUBTREE && !catalog_under(article.board,board)) ||
           !catalog_read(article.board,user))continue;
        int hidden=article_repository_unavailable(id);
        if(hidden<0)return ARTICLE_STORAGE;
        if(hidden || !article.created || article.created<since ||
           (until && article.created>until))continue;
        add_summary(out,id,&article);
    }
    out->next=id<=count?id:0;
    return ARTICLE_OK;
}

ArticleOutcome article_news(const ArticleActor *actor,uint32_t board,ArticleScope scope,
                            uint32_t start,uint64_t since,uint64_t until,ArticlePage *out) {
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=news_articles(actor,board,scope,start,since,until,out);
    policy_context(NULL);
    if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
    return result;
}

static int filter_text(const char *text,size_t max) {
    if(!text || !*text)return 0;
    size_t n=0;
    for(const unsigned char *p=(const unsigned char *)text;*p;p++)
        if(++n>max || *p<32 || *p==127)return 0;
    return 1;
}

static ArticleOutcome validate_filter(const ArticleActor *actor,const ArticleFilter *filter) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,1);
    if(result!=ARTICLE_OK)return result;
    if(!filter)return ARTICLE_INVALID_QUERY;
    if(filter->scope==ARTICLE_HERE && !catalog_read(filter->board,actor?actor->user:0))
        return ARTICLE_BOARD_DENIED;
    if((filter->scope!=ARTICLE_HERE && filter->scope!=ARTICLE_ALL && filter->scope!=ARTICLE_SUBTREE) ||
       !filter_text(filter->query,80) || !filter_text(filter->author,32))return ARTICLE_INVALID_QUERY;
    int known=!strcmp(filter->author,"*") || members_find(filter->author)!=0;
    if(members_broken())return ARTICLE_MEMBER_STORAGE;
    return known?ARTICLE_OK:ARTICLE_INVALID_QUERY;
}

/* -1 storage failure, 0 excluded, 1 visible exact match. */
static int filter_match(const ArticleActor *actor,const ArticleFilter *filter,
                        uint32_t id,ArticleMetadata *article) {
    if(!article_repository_find(id,article))return -1;
    if((filter->scope==ARTICLE_HERE && article->board!=filter->board) ||
       (filter->scope==ARTICLE_SUBTREE && !catalog_under(article->board,filter->board)) ||
       !catalog_read(article->board,actor?actor->user:0))return 0;
    int hidden=article_repository_unavailable(id);
    if(hidden)return hidden<0?-1:0;
    if(strcmp(filter->author,"*") && strcmp(filter->author,article->author))return 0;
    if(!strcmp(filter->query,"*") || strstr(article->title,filter->query))return 1;
    return article_repository_body_contains(id,filter->query);
}

static ArticleOutcome find_articles(const ArticleActor *actor,const ArticleFilter *filter,
                                    uint32_t start,ArticlePage *out) {
    ArticleOutcome result=validate_filter(actor,filter);
    if(result!=ARTICLE_OK)return result;
    uint32_t count=article_repository_count(),id=start?start:1;
    if(count>CONTENT_ID_MAX)return ARTICLE_STORAGE;
    if(id>CONTENT_ID_MAX+1)return ARTICLE_INVALID_CURSOR;
    unsigned scanned=0;
    for(;id<=count && out->count<8 && scanned<256;id++,scanned++) {
        uint32_t candidate=article_repository_search_next(id,filter->query);
        if(!candidate) { id=count+1;break; }
        if(candidate<id || candidate>count)return ARTICLE_STORAGE;
        id=candidate;
        ArticleMetadata article;int match=filter_match(actor,filter,id,&article);
        if(match<0)return ARTICLE_STORAGE;
        if(match)add_summary(out,id,&article);
    }
    out->next=id<=count?id:0;
    return ARTICLE_OK;
}

ArticleOutcome article_find(const ArticleActor *actor,const ArticleFilter *filter,
                            uint32_t start,ArticlePage *out) {
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=find_articles(actor,filter,start,out);
    policy_context(NULL);
    if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
    return result;
}

static ArticleOutcome scan_articles(const ArticleActor *actor,const ArticleFilter *filter,
                                    uint32_t start,int descending,ArticleScan *out) {
    ArticleOutcome result=validate_filter(actor,filter);
    if(result!=ARTICLE_OK)return result;
    uint32_t count=article_repository_count(),id=start?start:1;
    if(count>CONTENT_ID_MAX)return ARTICLE_STORAGE;
    if(descending && id>count)id=count;
    if(id>CONTENT_ID_MAX+1)return ARTICLE_INVALID_CURSOR;
    for(;id && id<=count;id=descending?id-1:id+1) {
        uint32_t candidate=article_repository_search_ordered(id,filter->query,descending!=0);
        if(!candidate)break;
        if(candidate>count || (descending?candidate>id:candidate<id))return ARTICLE_STORAGE;
        id=candidate;
        ArticleMetadata article;int match=filter_match(actor,filter,id,&article);
        if(match<0)return ARTICLE_STORAGE;
        if(!match)continue;
        out->article.id=id;out->article.board=article.board;out->article.parent=article.parent;
        memcpy(out->article.title,article.title,sizeof(out->article.title));
        out->next=descending?id-1:id<count?id+1:0;
        break;
    }
    return ARTICLE_OK;
}

ArticleOutcome article_scan(const ArticleActor *actor,const ArticleFilter *filter,
                            uint32_t start,int descending,ArticleScan *out) {
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=scan_articles(actor,filter,start,descending,out);
    policy_context(NULL);
    if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
    return result;
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

static ArticleOutcome mark_seen(const ArticleActor *actor,uint32_t board,uint32_t id,int seen) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,0);
    if(result!=ARTICLE_OK)return result;
    if(!catalog_read(board,actor->user))return ARTICLE_BOARD_DENIED;
    ArticleMetadata article;
    if(!article_repository_find(id,&article) || article.board!=board)return ARTICLE_NOT_FOUND;
    int hidden=article_repository_unavailable(id);
    if(hidden<0)return ARTICLE_STORAGE;
    if(hidden)return ARTICLE_HIDDEN;
    return members_mark(actor->user,id,!!seen)?ARTICLE_OK:ARTICLE_READ_STATE_STORAGE;
}

ArticleOutcome article_mark_seen(const ArticleActor *actor,uint32_t board,uint32_t id,int seen) {
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=mark_seen(actor,board,id,seen);
    policy_context(NULL);
    return result;
}

static ArticleOutcome unread_articles(const ArticleActor *actor,uint32_t board,uint32_t thread,
                                       uint32_t start,ArticleUnreadPage *out) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,0);
    if(result!=ARTICLE_OK)return result;
    if(!catalog_read(board,actor->user))return ARTICLE_BOARD_DENIED;
    uint32_t count=article_repository_count(),root=0;
    if(count>CONTENT_ID_MAX)return ARTICLE_STORAGE;
    if(thread) {
        ArticleMetadata article;
        if(!article_repository_find(thread,&article) || article.board!=board)return ARTICLE_NOT_FOUND;
        root=article.root;
        if(!root || root>count)return ARTICLE_STORAGE;
    }
    for(uint32_t id=1;id<=count;id++) {
        ArticleMetadata article;int visible=visible_summary(id,board,&article);
        if(visible<0)return ARTICLE_STORAGE;
        if(!visible || (root && article.root!=root))continue;
        int seen=members_seen(actor->user,id);
        if(members_broken())return ARTICLE_READ_STATE_STORAGE;
        if(seen)continue;
        out->total++;
        if(!out->first)out->first=id;
        if(id<start)continue;
        if(out->count==8) { if(!out->next)out->next=id;continue; }
        ArticleUnreadRow *row=&out->rows[out->count++];
        row->summary.id=id;row->summary.board=board;row->summary.parent=article.parent;
        memcpy(row->summary.title,article.title,sizeof(row->summary.title));row->root=article.root;
    }
    return ARTICLE_OK;
}

ArticleOutcome article_unread(const ArticleActor *actor,uint32_t board,uint32_t thread,
                              uint32_t start,ArticleUnreadPage *out) {
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=unread_articles(actor,board,thread,start,out);
    policy_context(NULL);
    if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
    return result;
}

static ArticleOutcome note_summary(uint32_t user,uint32_t board,uint32_t root,uint32_t count,
                                   uint32_t *visited,ArticleNoteRow *row) {
    for(uint32_t id=root;id;id=article_repository_tree_next(id)) {
        ArticleMetadata article;
        if(++(*visited)>count || id>count || !article_repository_find(id,&article) ||
           article.board!=board || article.root!=root ||
           (id==root?article.parent!=0:article.parent==0))return ARTICLE_STORAGE;
        int hidden=article_repository_unavailable(id);
        if(hidden<0)return ARTICLE_STORAGE;
        if(hidden)continue;
        if(id==root) {
            row->summary.id=id;row->summary.board=board;row->summary.parent=0;
            memcpy(row->summary.title,article.title,sizeof(row->summary.title));
        } else row->replies++;
        if(id>row->latest) { row->latest=id;row->updated=article.created; }
        if(user) {
            int seen=members_seen(user,id);
            if(members_broken())return ARTICLE_READ_STATE_STORAGE;
            if(!seen)row->unread++;
        }
    }
    return ARTICLE_OK;
}

static ArticleOutcome notes_articles(const ArticleActor *actor,uint32_t board,uint32_t start,
                                     int unread_only,ArticleNotesPage *out) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,!unread_only);
    if(result!=ARTICLE_OK)return result;
    uint32_t user=actor?actor->user:0;
    if(!catalog_read(board,user))return ARTICLE_BOARD_DENIED;
    uint32_t count=article_repository_count(),visited=0;
    if(count>CONTENT_ID_MAX)return ARTICLE_STORAGE;
    out->unread_known=!!user;
    for(uint32_t id=1;id<=count;id++) {
        ArticleMetadata article;
        if(!article_repository_find(id,&article))return ARTICLE_STORAGE;
        if(article.board!=board || article.parent)continue;
        int hidden=article_repository_unavailable(id);
        if(hidden<0)return ARTICLE_STORAGE;
        if(hidden)continue;
        ArticleNoteRow row={0};
        result=note_summary(user,board,id,count,&visited,&row);
        if(result!=ARTICLE_OK)return result;
        if(unread_only && !row.unread)continue;
        out->total++;
        if(id<start)continue;
        if(out->count<5)out->rows[out->count++]=row;
        else if(!out->next)out->next=id;
    }
    return ARTICLE_OK;
}

ArticleOutcome article_notes(const ArticleActor *actor,uint32_t board,uint32_t start,
                             int unread_only,ArticleNotesPage *out) {
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=notes_articles(actor,board,start,unread_only,out);
    policy_context(NULL);
    if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
    return result;
}
