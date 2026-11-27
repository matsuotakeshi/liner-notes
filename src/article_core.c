#include "article_core.h"
#include "article_repository.h"
#include "members.h"
#include "catalog.h"
#include "moderation.h"
#include "board_policy.h"
#include "extras.h"
#include "profile_store.h"
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

static ArticleOutcome posting_access(const ArticleActor *actor,uint32_t board,uint32_t parent,
                                     ArticlePostingPolicy *rules) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,1);
    if(result!=ARTICLE_OK)return result;
    uint32_t user=actor?actor->user:0;
    if(!catalog_read(board,user))return ARTICLE_BOARD_DENIED;
    if(!article_repository_posting_policy(board,user,rules) || rules->version<1 || rules->version>4)
        return ARTICLE_STORAGE;
    if(rules->members_required && !user && !rules->guest_allowed)return ARTICLE_LOGIN_REQUIRED;
    if(parent) {
        ArticleMetadata article;
        if(!article_repository_find(parent,&article) || article.board!=board)return ARTICLE_NOT_FOUND;
        int hidden=article_repository_unavailable(parent);
        if(hidden<0)return ARTICLE_STORAGE;
        if(hidden)return ARTICLE_HIDDEN;
    }
    if(!catalog_write(board,user))return ARTICLE_POSTING_DENIED;
    if(rules->version<4 && board!=1)return ARTICLE_LEGACY_BOARDS;
    return ARTICLE_OK;
}

ArticleOutcome article_prepare_post(const ArticleActor *actor,uint32_t board,uint32_t parent) {
    ArticlePostingPolicy rules;
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=posting_access(actor,board,parent,&rules);
    policy_context(NULL);
    return result;
}

static size_t post_length(const char *text,size_t limit) {
    if(!text)return 0;
    size_t n=0;while(n<=limit && text[n])n++;return n;
}

static int post_plain_text(const char *text) {
    for(const unsigned char *p=(const unsigned char *)text;*p;p++)
        if(*p<32 || *p==127)return 0;
    return 1;
}

static ArticleOutcome post_local(const ArticleActor *actor,uint32_t board,uint32_t parent,
                                 const char *guest_name,const char *title,const char *body,uint32_t *out_id) {
    ArticlePostingPolicy rules;
    ArticleOutcome result=posting_access(actor,board,parent,&rules);
    if(result!=ARTICLE_OK)return result;
    if(rules.members_required && rules.version<3)return ARTICLE_LEGACY_MEMBERS;
    if(rules.storage_failed)return ARTICLE_POST_UNAVAILABLE;
    uint32_t count=article_repository_count(),user=actor?actor->user:0;
    if(parent>count)return ARTICLE_NOT_FOUND;
    size_t tl=post_length(title,80),bl=post_length(body,CONTENT_BODY);
    if(bl>rules.body_limit)return ARTICLE_BODY_LIMIT;
    if(count>=CONTENT_ID_MAX || (rules.total_limit && count>=rules.total_limit) ||
       (user && rules.user_limit && rules.user_posts>=rules.user_limit))return ARTICLE_FULL;
    int fits=article_repository_post_capacity(board,bl);
    if(fits<0)return ARTICLE_STORAGE;
    if(!fits)return ARTICLE_FULL;
    if(!tl || tl>80 || !bl || bl>CONTENT_BODY || !post_plain_text(title))return ARTICLE_INVALID_DRAFT;
    if(bl>1024 && rules.version!=4)return ARTICLE_LEGACY_BODY;
    const char *author=user?members_name(user):guest_name;
    if(!author || !*author)author="guest";
    if(post_length(author,32)>32 || !post_plain_text(author))return ARTICLE_INVALID_DRAFT;
    uint32_t id=0;
    if(!article_repository_append(board,parent,user,author,title,body,&id) || id!=count+1)
        return ARTICLE_POST_UNCERTAIN;
    *out_id=id;return ARTICLE_OK;
}

ArticleOutcome article_post(const ArticleActor *actor,uint32_t board,uint32_t parent,
                            const char *guest_name,const char *title,const char *body,uint32_t *out_id) {
    if(!out_id)return ARTICLE_INVALID_DRAFT;
    *out_id=0;
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=post_local(actor,board,parent,guest_name,title,body,out_id);
    policy_context(NULL);
    if(result!=ARTICLE_OK)*out_id=0;
    return result;
}

typedef enum { DRAFT_STATUS, DRAFT_RESUME, DRAFT_SAVE, DRAFT_REMOVE } DraftAction;

static ArticleOutcome saved_draft(const ArticleActor *actor,DraftAction action,
                                  uint32_t board,uint32_t parent,const char *title,const char *body,
                                  uint32_t expected,ArticleDraftView *out,char *text,size_t capacity) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,0);
    if(result!=ARTICLE_OK)return result;
    MemberDraft draft={0};
    int exists=members_draft(actor->user,&draft);
    /* A failed/partial body read must not look like an absent saved copy. */
    if(members_broken())return ARTICLE_MEMBER_STORAGE;
    if(action==DRAFT_STATUS) {
        if(exists)out->revision=draft.revision;
        return ARTICLE_OK;
    }
    if(action==DRAFT_REMOVE) {
        int ok=members_store_draft(actor->user,&draft,expected,1,&out->revision);
        return ok==1?ARTICLE_OK:ok==0?ARTICLE_DRAFT_CONFLICT:ARTICLE_MEMBER_STORAGE;
    }
    if(action==DRAFT_SAVE) { draft.board=board;draft.parent=parent; }
    else if(!exists)return ARTICLE_OK;
    if(!catalog_read(draft.board,actor->user) || !catalog_write(draft.board,actor->user))
        return ARTICLE_BOARD_DENIED;
    if(draft.parent) {
        ArticleMetadata article;
        if(!article_repository_find(draft.parent,&article) || article.board!=draft.board)return ARTICLE_NOT_FOUND;
        int unavailable=article_repository_unavailable(draft.parent);
        if(unavailable<0)return ARTICLE_STORAGE;
        if(unavailable)return ARTICLE_HIDDEN;
    }
    if(action==DRAFT_SAVE) {
        size_t tl=post_length(title,80),bl=post_length(body,CONTENT_BODY);
        if(!tl || tl>80 || !body || bl>CONTENT_BODY)return ARTICLE_INVALID_DRAFT;
        memcpy(draft.title,title,tl+1);memcpy(draft.body,body,bl+1);
        int ok=members_store_draft(actor->user,&draft,expected,0,&out->revision);
        return ok==1?ARTICLE_OK:ok==0?ARTICLE_DRAFT_CONFLICT:ARTICLE_MEMBER_STORAGE;
    }
    size_t length=post_length(draft.body,CONTENT_BODY);
    if(length>CONTENT_BODY)return ARTICLE_MEMBER_STORAGE;
    if(!text || capacity<=length)return ARTICLE_BUFFER_TOO_SMALL;
    out->board=draft.board;out->parent=draft.parent;out->revision=draft.revision;
    memcpy(out->title,draft.title,sizeof(out->title));memcpy(text,draft.body,length+1);
    return ARTICLE_OK;
}

static ArticleOutcome draft_call(const ArticleActor *actor,DraftAction action,
                                 uint32_t board,uint32_t parent,const char *title,const char *body,
                                 uint32_t expected,ArticleDraftView *out,char *text,size_t capacity) {
    if(text && capacity)memset(text,0,capacity);
    if(!out)return ARTICLE_INVALID_DRAFT;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=saved_draft(actor,action,board,parent,title,body,expected,out,text,capacity);
    policy_context(NULL);
    if(result!=ARTICLE_OK) {
        memset(out,0,sizeof(*out));
        if(text && capacity)memset(text,0,capacity);
    }
    return result;
}

ArticleOutcome article_draft_status(const ArticleActor *actor,uint32_t *revision) {
    if(!revision)return ARTICLE_INVALID_DRAFT;
    ArticleDraftView view;
    ArticleOutcome result=draft_call(actor,DRAFT_STATUS,0,0,NULL,NULL,0,&view,NULL,0);
    *revision=view.revision;return result;
}
ArticleOutcome article_draft_resume(const ArticleActor *actor,ArticleDraftView *out,char *body,size_t capacity) {
    return draft_call(actor,DRAFT_RESUME,0,0,NULL,NULL,0,out,body,capacity);
}
ArticleOutcome article_draft_save(const ArticleActor *actor,uint32_t board,uint32_t parent,
                                 const char *title,const char *body,uint32_t expected,uint32_t *revision) {
    if(!revision)return ARTICLE_INVALID_DRAFT;
    ArticleDraftView view;
    ArticleOutcome result=draft_call(actor,DRAFT_SAVE,board,parent,title,body,expected,&view,NULL,0);
    *revision=view.revision;return result;
}
ArticleOutcome article_draft_remove(const ArticleActor *actor,uint32_t expected,uint32_t *revision) {
    if(!revision)return ARTICLE_INVALID_DRAFT;
    ArticleDraftView view;
    ArticleOutcome result=draft_call(actor,DRAFT_REMOVE,0,0,NULL,NULL,expected,&view,NULL,0);
    *revision=view.revision;return result;
}

static ArticleOutcome enter_board(const ArticleActor *actor,uint32_t board,ArticleBoardEntry *out) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,1);
    if(result!=ARTICLE_OK)return result;
    uint32_t user=actor?actor->user:0;
    if(!board || board>CATALOG_MAX || !catalog_visible(board,user))return ARTICLE_BOARD_DENIED;
    int password_board=policy_locked(board,user);
    if(password_board<0 || password_board>CATALOG_MAX)return ARTICLE_POLICY_STORAGE;
    if(password_board) {
        out->id=board;out->password_board=(uint32_t)password_board;return ARTICLE_OK;
    }
    const char *title=catalog_title(board);
    if(!title || post_length(title,80)>80)return ARTICLE_STORAGE;
    out->id=board;out->menu=policy_menu(board)!=0;strcpy(out->title,title);
    return ARTICLE_OK;
}

ArticleOutcome article_enter_board(const ArticleActor *actor,uint32_t board,ArticleBoardEntry *out) {
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=enter_board(actor,board,out);
    policy_context(NULL);
    if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
    return result;
}

static ArticleOutcome board_info(const ArticleActor *actor,uint32_t board,ArticleBoardInfo *out,
                                 char *information,size_t capacity) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,1);
    if(result!=ARTICLE_OK)return result;
    if(!board || board>CATALOG_MAX || !catalog_read(board,actor?actor->user:0))return ARTICLE_BOARD_DENIED;
    const char *title=catalog_title(board),*description=catalog_description(board),*info=policy_info(board);
    if(!title || !description || !info || post_length(title,80)>80 || post_length(description,320)>320)
        return ARTICLE_STORAGE;
    size_t length=post_length(info,BOARD_INFO_MAX);
    if(length>BOARD_INFO_MAX)return ARTICLE_POLICY_STORAGE;
    if(!information || capacity<=length)return ARTICLE_BUFFER_TOO_SMALL;
    out->id=board;out->information_size=length;
    strcpy(out->title,title);strcpy(out->description,description);memcpy(information,info,length+1);
    return ARTICLE_OK;
}

ArticleOutcome article_board_info(const ArticleActor *actor,uint32_t board,ArticleBoardInfo *out,
                                  char *information,size_t capacity) {
    if(information && capacity)memset(information,0,capacity);
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=board_info(actor,board,out,information,capacity);
    policy_context(NULL);
    if(result!=ARTICLE_OK) {
        memset(out,0,sizeof(*out));
        if(information && capacity)memset(information,0,capacity);
    }
    return result;
}

static ArticleOutcome audit_page(const ArticleActor *actor,uint32_t board,uint32_t start,
                                 uint32_t article,uint32_t operator_id,const char *operator_name,ArticleAuditPage *out) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,1);
    if(result!=ARTICLE_OK)return result;
    uint32_t user=actor?actor->user:0;
    if(!user || !catalog_manage(board,user))return ARTICLE_ADMIN_REQUIRED;
    if(operator_name && *operator_name) {
        if(operator_id || post_length(operator_name,32)>32)return ARTICLE_INVALID_QUERY;
        operator_id=members_find(operator_name);
        if(members_broken())return ARTICLE_MEMBER_STORAGE;
        if(!operator_id)return ARTICLE_INVALID_QUERY;
    }
    if(operator_id && !members_name(operator_id))return ARTICLE_INVALID_QUERY;
    uint32_t mod_count=moderation_count();
    uint64_t total=(uint64_t)mod_count+extras_audit_count();
    if(total>UINT32_MAX)return ARTICLE_STORAGE;
    if(start>total)return ARTICLE_INVALID_CURSOR;
    uint32_t id=start?start:(uint32_t)total;
    out->start=id;out->article=article;out->actor=operator_id;
    int admin=moderation_admin(user);
    for(unsigned scanned=0;id && out->count<8 && scanned<256;id--,scanned++) {
        ModerationEntry entry={0};uint32_t mod_sequence=0;
        if(!extras_audit_entry(id,&entry,&mod_sequence))return ARTICLE_EXTRAS_STORAGE;
        if(mod_sequence && (mod_sequence>mod_count || !moderation_entry(mod_sequence,&entry)))
            return ARTICLE_MODERATION_STORAGE;
        if(!mod_sequence) {
            ArticleMetadata metadata;
            entry.board=article_repository_find(entry.target,&metadata)?metadata.board:0;
        }
        int article_event=entry.kind=='M' || entry.kind=='D' || entry.kind=='T' || entry.kind=='A';
        if(!admin && (!article_event || !catalog_sigop(entry.board,user)))continue;
        if(article_event && !catalog_read(entry.board,user))continue;
        if(article && (!article_event || entry.target!=article))continue;
        if(operator_id && entry.actor!=operator_id)continue;
        ArticleAuditRow *row=&out->rows[out->count];
        switch(entry.kind) {
        case 'M': row->action=entry.enabled?ARTICLE_AUDIT_HIDE:ARTICLE_AUDIT_RESTORE;break;
        case 'D': row->action=ARTICLE_AUDIT_WITHDRAW;break;
        case 'T': row->action=ARTICLE_AUDIT_RETITLE;break;
        case 'A': row->action=ARTICLE_AUDIT_AUTHOR;break;
        case 'K': row->action=ARTICLE_AUDIT_CHAT_KICK;break;
        case 'S': row->action=entry.enabled?ARTICLE_AUDIT_SUSPEND:ARTICLE_AUDIT_RESUME;break;
        default: return mod_sequence?ARTICLE_MODERATION_STORAGE:ARTICLE_EXTRAS_STORAGE;
        }
        const char *name=members_name(entry.actor);
        if(!name || post_length(name,32)>32)return ARTICLE_MEMBER_STORAGE;
        row->sequence=id;row->actor=entry.actor;row->target=entry.target;row->board=entry.board;
        row->time=entry.time;strcpy(row->actor_name,name);out->count++;
    }
    out->next=id;return ARTICLE_OK;
}

ArticleOutcome article_audit(const ArticleActor *actor,uint32_t current_board,uint32_t start,
                             uint32_t article,uint32_t actor_id,const char *actor_name,ArticleAuditPage *out) {
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=audit_page(actor,current_board,start,article,actor_id,actor_name,out);
    policy_context(NULL);
    if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
    return result;
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

static ArticleOutcome wani_change(const ArticleActor *actor,uint32_t board,uint32_t id,int confirmed,const char *title) {
    ArticleOutcome result=authorize(actor,board,id);
    if(result!=ARTICLE_OK)return result;
    int unavailable=article_repository_unavailable(id);
    if(unavailable<0)return ARTICLE_STORAGE;
    if(unavailable)return ARTICLE_HIDDEN;
    if(!catalog_manage(board,actor->user)) {
        ArticleMetadata latest;
        for(uint32_t next=article_repository_count();next>id;next--) {
            if(!article_repository_find(next,&latest))return ARTICLE_STORAGE;
            if(latest.board==board)return ARTICLE_OWNER_REQUIRED;
        }
    }
    return title?retitle(actor,board,id,title):withdraw(actor,board,id,confirmed);
}

ArticleOutcome article_wani_change(const ArticleActor *actor,uint32_t board,uint32_t id,int confirmed,const char *title) {
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=wani_change(actor,board,id,confirmed,title);
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
    out->root=article.root;
    out->created=article.created;
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

/* Catalog-sized scratch only: no per-article array or body materialization. */
static ArticleOutcome board_unread_counts(const ArticleActor *actor,int *readable,uint32_t *unread) {
    uint32_t user=actor?actor->user:0;
    for(uint32_t board=1;board<=CATALOG_MAX;board++)readable[board]=catalog_read(board,user);
    if(!user)return ARTICLE_OK;
    uint32_t count=article_repository_count();
    if(count>CONTENT_ID_MAX)return ARTICLE_STORAGE;
    for(uint32_t id=1;id<=count;id++) {
        ArticleMetadata article;
        if(!article_repository_find(id,&article) || !article.board || article.board>CATALOG_MAX)
            return ARTICLE_STORAGE;
        if(!readable[article.board])continue;
        int hidden=article_repository_unavailable(id);
        if(hidden<0)return ARTICLE_STORAGE;
        if(hidden)continue;
        int seen=members_seen(user,id);
        if(members_broken())return ARTICLE_READ_STATE_STORAGE;
        if(!seen)unread[article.board]++;
    }
    return ARTICLE_OK;
}

static ArticleOutcome board_summary(const ArticleActor *actor,uint32_t id,int readable,
                                    uint32_t unread,ArticleBoardRow *out) {
    const char *title=catalog_title(id);
    uint32_t parent=catalog_parent(id);unsigned depth=catalog_depth(id);
    if(!title || strlen(title)>80 || parent>CATALOG_MAX || parent==id || depth>=CATALOG_MAX)
        return ARTICLE_STORAGE;
    out->id=id;out->parent=parent;out->depth=depth;
    out->writable=!!catalog_write(id,actor?actor->user:0);
    out->unread_known=actor && actor->user && readable;
    out->unread=out->unread_known?unread:0;
    memcpy(out->title,title,strlen(title)+1);
    return ARTICLE_OK;
}

static ArticleOutcome list_boards(const ArticleActor *actor,uint32_t start,int children_only,
                                  ArticleBoardsPage *out) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,1);
    if(result!=ARTICLE_OK)return result;
    uint32_t first=start?start:1,user=actor?actor->user:0;
    if(first>CATALOG_MAX || !catalog_exists(first))return ARTICLE_BOARD_DENIED;
    int readable[CATALOG_MAX+1]={0};uint32_t unread[CATALOG_MAX+1]={0};
    unsigned char visited[CATALOG_MAX+1]={0};
    result=board_unread_counts(actor,readable,unread);
    if(result!=ARTICLE_OK)return result;
    for(uint32_t id=first;id;id=catalog_next(id)) {
        if(id>CATALOG_MAX || !catalog_exists(id) || visited[id])return ARTICLE_STORAGE;
        visited[id]=1;
        if(!catalog_visible(id,user) || (children_only && catalog_parent(id)!=first))continue;
        if(out->count==8) { out->next=id;return ARTICLE_OK; }
        result=board_summary(actor,id,readable[id],unread[id],&out->rows[out->count]);
        if(result!=ARTICLE_OK)return result;
        out->count++;
    }
    return ARTICLE_OK;
}

ArticleOutcome article_boards(const ArticleActor *actor,uint32_t start,int children_only,
                              ArticleBoardsPage *out) {
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=list_boards(actor,start,children_only,out);
    policy_context(NULL);
    if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
    return result;
}

static ArticleOutcome board_children(const ArticleActor *actor,uint32_t parent,uint32_t start,
                                      ArticleBoardsPage *out) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,0);
    if(result!=ARTICLE_OK)return result;
    if(parent && !catalog_read(parent,actor->user))return ARTICLE_BOARD_DENIED;
    unsigned char visited[CATALOG_MAX+1]={0};
    for(uint32_t id=start?start:1;id;id=catalog_next(id)) {
        if(id>CATALOG_MAX || !catalog_exists(id) || visited[id])return ARTICLE_STORAGE;
        visited[id]=1;
        if(catalog_parent(id)!=parent || !catalog_visible(id,actor->user))continue;
        if(out->count==8) {
            out->next=id;
            break;
        }
        result=board_summary(actor,id,0,0,&out->rows[out->count]);
        if(result!=ARTICLE_OK)return result;
        out->count++;
    }
    return ARTICLE_OK;
}

ArticleOutcome article_board_children(const ArticleActor *actor,uint32_t parent,uint32_t start,
                                      ArticleBoardsPage *out) {
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=board_children(actor,parent,start,out);
    policy_context(NULL);
    if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
    return result;
}

ArticleOutcome article_board_named(const ArticleActor *actor,const char *title,uint32_t *id) {
    if(!id || !title || !*title)return ARTICLE_INVALID_QUERY;
    *id=0;
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,0);
    if(result!=ARTICLE_OK)return result;
    policy_context(actor->board_grants);
    uint32_t match=0;
    for(uint32_t n=1;n<=CATALOG_MAX;n++) {
        const char *name=catalog_title(n);
        if(!catalog_visible(n,actor->user) || !name || strcmp(name,title))continue;
        if(match) { result=ARTICLE_INVALID_QUERY;break; }
        match=n;
    }
    policy_context(NULL);
    if(result!=ARTICLE_OK)return result;
    if(!match)return ARTICLE_BOARD_DENIED;
    *id=match;
    return ARTICLE_OK;
}

static ArticleOutcome next_unread_board(const ArticleActor *actor,uint32_t current,ArticleBoardRow *out) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,0);
    if(result!=ARTICLE_OK)return result;
    if(!current || current>CATALOG_MAX || !catalog_exists(current))return ARTICLE_BOARD_DENIED;
    int readable[CATALOG_MAX+1]={0};uint32_t unread[CATALOG_MAX+1]={0};
    unsigned char visited[CATALOG_MAX+1]={0};
    result=board_unread_counts(actor,readable,unread);
    if(result!=ARTICLE_OK)return result;
    uint32_t next=current;
    for(unsigned n=0;n<CATALOG_MAX;n++) {
        next=catalog_next(next);if(!next)next=1;
        if(next==current)return ARTICLE_OK;
        if(next>CATALOG_MAX || !catalog_exists(next) || visited[next])return ARTICLE_STORAGE;
        visited[next]=1;
        if(readable[next] && unread[next])return board_summary(actor,next,1,unread[next],out);
    }
    return ARTICLE_STORAGE;
}

ArticleOutcome article_next_board(const ArticleActor *actor,uint32_t current,ArticleBoardRow *out) {
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=next_unread_board(actor,current,out);
    policy_context(NULL);
    if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
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
    memcpy(row->author,article->author,sizeof(row->author));
    row->created=article->created;
}

static ArticleOutcome browse_notes(const ArticleActor *actor,uint32_t board,uint32_t root,
                                   uint32_t start,int descending,int unread,const char *query,
                                   int flat,ArticlePage *out) {
    ArticleOutcome result=health();
    if(result!=ARTICLE_OK)return result;
    result=identity(actor,0);
    if(result!=ARTICLE_OK)return result;
    if(!catalog_read(board,actor->user))return ARTICLE_BOARD_DENIED;
    ArticleMetadata entry;
    if(root) {
        int visible=visible_summary(root,board,&entry);
        if(visible<0)return ARTICLE_STORAGE;
        if(!visible || entry.parent)return ARTICLE_NOT_FOUND;
    }
    uint32_t count=article_repository_count();
    if(count>CONTENT_ID_MAX)return ARTICLE_STORAGE;
    uint32_t id=start?start:(descending?count:1);
    if(descending && id>count)id=count;
    for(unsigned scanned=0;id && id<=count && out->count<8 && scanned<256;scanned++) {
        int visible=visible_summary(id,board,&entry);
        if(visible<0)return ARTICLE_STORAGE;
        uint32_t current=id;
        id=descending?id-1:id+1;
        if(!visible || (!flat && (root?(entry.root!=root || !entry.parent):entry.parent!=0)))continue;
        if(query && !strstr(entry.title,query))continue;
        if(unread && members_seen(actor->user,current))continue;
        if(members_broken())return ARTICLE_READ_STATE_STORAGE;
        add_summary(out,current,&entry);
    }
    out->next=id<=count?id:0;
    return ARTICLE_OK;
}

ArticleOutcome article_browse_notes(const ArticleActor *actor,uint32_t board,uint32_t root,
                                    uint32_t start,int descending,int unread,const char *query,
                                    ArticlePage *out) {
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=browse_notes(actor,board,root,start,descending,unread,query,0,out);
    policy_context(NULL);
    if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
    return result;
}

ArticleOutcome article_browse_board(const ArticleActor *actor,uint32_t board,uint32_t start,
                                    int descending,ArticlePage *out) {
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=browse_notes(actor,board,0,start,descending,0,NULL,1,out);
    policy_context(NULL);
    if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
    return result;
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
                                    uint32_t start,uint64_t since,uint64_t until,int unread,ArticlePage *out) {
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
        if(unread && members_seen(user,id))continue;
        if(unread && members_broken())return ARTICLE_READ_STATE_STORAGE;
        add_summary(out,id,&article);
    }
    out->next=id<=count?id:0;
    return ARTICLE_OK;
}

ArticleOutcome article_news_filtered(const ArticleActor *actor,uint32_t board,ArticleScope scope,
                                     uint32_t start,uint64_t since,uint64_t until,int unread,ArticlePage *out) {
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=news_articles(actor,board,scope,start,since,until,unread,out);
    policy_context(NULL);
    if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
    return result;
}

ArticleOutcome article_news(const ArticleActor *actor,uint32_t board,ArticleScope scope,
                            uint32_t start,uint64_t since,uint64_t until,ArticlePage *out) {
    return article_news_filtered(actor,board,scope,start,since,until,0,out);
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
                                   uint32_t *visited,uint64_t since,uint64_t until,ArticleNoteRow *row) {
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
            row->created=article.created;
            memcpy(row->author,article.author,sizeof(row->author));
        } else row->replies++;
        if(id>row->latest) { row->latest=id;row->updated=article.created; }
        if(user && article.created>=since && (!until || article.created<=until)) {
            int seen=members_seen(user,id);
            if(members_broken())return ARTICLE_READ_STATE_STORAGE;
            if(!seen)row->unread++;
        }
    }
    return ARTICLE_OK;
}

static ArticleOutcome notes_articles(const ArticleActor *actor,uint32_t board,uint32_t start,
                                     int unread_only,uint64_t since,uint64_t until,ArticleNotesPage *out) {
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
        result=note_summary(user,board,id,count,&visited,since,until,&row);
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
    ArticleOutcome result=notes_articles(actor,board,start,unread_only,0,0,out);
    policy_context(NULL);
    if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
    return result;
}

ArticleOutcome article_notes_window(const ArticleActor *actor,uint32_t board,uint32_t start,
                                    uint64_t since,uint64_t until,ArticleNotesPage *out) {
    if(!out)return ARTICLE_INVALID_READ;
    memset(out,0,sizeof(*out));
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=notes_articles(actor,board,start,1,since,until,out);
    policy_context(NULL);
    if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
    return result;
}

static ArticleOutcome import_buffer(const ArticleActor *actor,uint32_t board,uint32_t id,char *scratch,size_t capacity) {
    ArticleOutcome result=health();if(result!=ARTICLE_OK)return result;
    result=identity(actor,0);if(result!=ARTICLE_OK)return result;
    if(!scratch || !capacity)return ARTICLE_BUFFER_TOO_SMALL;
    if(profile_store_broken())return ARTICLE_STORAGE;
    ArticleMetadata metadata;
    if(!article_repository_find(id,&metadata) || metadata.board!=board)return ARTICLE_NOT_FOUND;
    ArticleView view;
    memset(&view,0,sizeof(view));
    result=read_article(actor,id,0,&view,scratch,capacity);
    if(result!=ARTICLE_OK)return result;
    if(view.state!=ARTICLE_VISIBLE)return ARTICLE_NOT_FOUND;
    size_t n=0;while(n<capacity && scratch[n])n++;
    if(n==capacity || n!=view.body_size)return ARTICLE_STORAGE;
    if(profile_set(actor->user,PROFILE_BUFFER,scratch)!=1 || profile_store_broken())return ARTICLE_STORAGE;
    return ARTICLE_OK;
}
ArticleOutcome article_import_buffer(const ArticleActor *actor,uint32_t board,uint32_t id,char *scratch,size_t capacity) {
    if(scratch && capacity)memset(scratch,0,capacity);
    policy_context(actor?actor->board_grants:NULL);
    ArticleOutcome result=import_buffer(actor,board,id,scratch,capacity);
    policy_context(NULL);
    if(scratch && capacity)memset(scratch,0,capacity);
    return result;
}

static ArticleOutcome management_identity(const ArticleActor *actor) {
    ArticleOutcome result=health();
    return result==ARTICLE_OK?identity(actor,0):result;
}

ArticleOutcome article_author_set(const ArticleActor *actor,uint32_t id,uint32_t target) {
    ArticleOutcome result=management_identity(actor);
    if(result!=ARTICLE_OK)return result;
    ArticleMetadata article;
    if(!id || !article_repository_find(id,&article))return ARTICLE_ADMIN_REQUIRED;
    int hidden=article_repository_unavailable(id);
    if(hidden<0)return ARTICLE_STORAGE;
    if(hidden || !catalog_manage(article.board,actor->user))return ARTICLE_ADMIN_REQUIRED;
    if(!members_active(target) || members_pending(target) || moderation_suspended(target))
        return ARTICLE_MANAGEMENT_INVALID;
    const char *name=members_name(target);
    if(!name || !*name || strlen(name)>32)return ARTICLE_MANAGEMENT_INVALID;
    /* Copy metadata before publication: no repository cache pointer survives. */
    char author[33];strcpy(author,name);
    if(!extras_set('A',id,actor->user,author))return ARTICLE_SETTINGS_STORAGE;
    article_repository_author_changed(id,target,author);
    return ARTICLE_OK;
}

ArticleOutcome article_guest_set(const ArticleActor *actor,uint32_t board,unsigned enabled) {
    ArticleOutcome result=management_identity(actor);
    if(result!=ARTICLE_OK)return result;
    if(!catalog_manage(board,actor->user))return ARTICLE_ADMIN_REQUIRED;
    if(enabled>1)return ARTICLE_MANAGEMENT_INVALID;
    if(!article_repository_guest_save(board,enabled))return ARTICLE_SETTINGS_STORAGE;
    catalog_guest(board,(int)enabled);
    return ARTICLE_OK;
}

ArticleOutcome article_sigop_set(const ArticleActor *actor,uint32_t board,uint32_t target,ArticleRoleChange *out) {
    if(!out)return ARTICLE_MANAGEMENT_INVALID;
    memset(out,0,sizeof(*out));
    ArticleOutcome result=management_identity(actor);
    if(result!=ARTICLE_OK)return result;
    if(!catalog_exists(board) || (!moderation_admin(actor->user) &&
       !catalog_sigop(catalog_parent(board),actor->user)) ||
       (target && (!members_name(target) || members_pending(target) || moderation_suspended(target))))
        return ARTICLE_MANAGEMENT_INVALID;
    uint32_t previous=0;
    int ok=catalog_role_set(board,target,actor->user,&previous);
    if(ok<0)return ARTICLE_CATALOG_STORAGE;
    if(!ok)return ARTICLE_MANAGEMENT_INVALID;
    if(ok==1) {
        out->changed=1;
        if(previous) {
            moderation_credentials_changed(previous);
            out->previous=previous;out->previous_epoch=moderation_epoch(previous);
        }
        if(target) {
            moderation_credentials_changed(target);
            out->current=target;out->current_epoch=moderation_epoch(target);
        }
    }
    return ARTICLE_OK;
}
