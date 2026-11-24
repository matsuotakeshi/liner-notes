#include "mmm_notes_repository.h"
#include "catalog.h"
#include "identity_legacy.h"
#include "moderation.h"
#include <string.h>

static ArticleOutcome authorize(const ArticleActor *actor) {
    if(!actor || !actor->user)return ARTICLE_LOGIN_REQUIRED;
    IdentityProvider identity=identity_legacy_provider();
    Principal principal;
    return identity.resolve(identity.context,actor->user,actor->epoch,&principal)?ARTICLE_OK:ARTICLE_REVOKED;
}

static int canonical_label(const char *label,char out[MMM_LABEL_MAX+1]) {
    size_t n=0;
    for(;n<=MMM_LABEL_MAX && label[n];n++) {
        unsigned char c=(unsigned char)label[n];
        if(c>='A' && c<='Z')c=(unsigned char)(c+'a'-'A');
        if(!((c>='a' && c<='z') || (c>='0' && c<='9') ||
             (n && (c=='_' || c=='-' || c=='.'))))return 0;
        if(n<MMM_LABEL_MAX)out[n]=(char)c;
    }
    if(!n || n>MMM_LABEL_MAX)return 0;
    out[n]=0;
    return 1;
}

static ArticleOutcome find_label(const char *name,uint32_t *board) {
    char label[MMM_LABEL_MAX+1];
    if(!canonical_label(name,label))return ARTICLE_INVALID_QUERY;
    if(!mmm_notes_label_lookup(label,board))return ARTICLE_SETTINGS_STORAGE;
    return *board?ARTICLE_OK:ARTICLE_BOARD_DENIED;
}

ArticleOutcome mmm_note_label_resolve(const ArticleActor *actor,const char *name,uint32_t *out) {
    *out=0;
    ArticleOutcome result=authorize(actor);
    if(result!=ARTICLE_OK)return result;
    uint32_t board;
    result=find_label(name,&board);
    if(result!=ARTICLE_OK)return result;
    ArticleBoardEntry entry;
    result=article_enter_board(actor,board,&entry);
    if(result==ARTICLE_OK)*out=entry.id;
    return result;
}

ArticleOutcome mmm_note_label(const ArticleActor *actor,uint32_t board,const char *replacement,char out[MMM_LABEL_MAX+1]) {
    *out=0;
    ArticleOutcome result=authorize(actor);
    if(result!=ARTICLE_OK)return result;
    if(!moderation_admin(actor->user))return ARTICLE_ADMIN_REQUIRED;
    if(!board || board>CATALOG_MAX)return ARTICLE_BOARD_DENIED;
    if(!replacement) {
        if(!catalog_known(board))return ARTICLE_BOARD_DENIED;
        return mmm_notes_label_get(board,out)?ARTICLE_OK:ARTICLE_SETTINGS_STORAGE;
    }
    char label[MMM_LABEL_MAX+1]="";
    if(*replacement && !canonical_label(replacement,label))return ARTICLE_INVALID_QUERY;
    if(!strcmp(label,"none"))label[0]=0;
    if(*label && !catalog_exists(board))return ARTICLE_BOARD_DENIED;
    int saved=mmm_notes_label_save(board,label);
    if(saved!=1)return saved<0?ARTICLE_INVALID_QUERY:ARTICLE_SETTINGS_STORAGE;
    strcpy(out,label);
    return ARTICLE_OK;
}

ArticleOutcome mmm_note_scanlist(const ArticleActor *actor,MmmScanList *out) {
    memset(out,0,sizeof(*out));
    ArticleOutcome result=authorize(actor);
    if(result!=ARTICLE_OK)return result;
    uint32_t boards[MMM_SCAN_MAX];unsigned count;
    if(!mmm_notes_scanlist_load(actor->user,boards,&count))return ARTICLE_SETTINGS_STORAGE;
    out->custom=count!=0;
    for(unsigned i=0;i<count;i++) {
        ArticleBoardEntry entry;
        result=article_enter_board(actor,boards[i],&entry);
        if(result==ARTICLE_BOARD_DENIED || (result==ARTICLE_OK && (entry.password_board || entry.menu)))continue;
        if(result!=ARTICLE_OK)break;
        MmmScanBoard *row=&out->rows[out->count];
        if(!mmm_notes_label_get(entry.id,row->label)) { result=ARTICLE_SETTINGS_STORAGE;break; }
        row->board=entry.id;strcpy(row->title,entry.title);
        out->count++;
    }
    if(result!=ARTICLE_OK && result!=ARTICLE_BOARD_DENIED) { memset(out,0,sizeof(*out));return result; }
    return ARTICLE_OK;
}

ArticleOutcome mmm_note_scanlist_save(const ArticleActor *actor,const MmmScanTargets *list) {
    ArticleOutcome result=authorize(actor);
    if(result!=ARTICLE_OK)return result;
    if(!list || list->count>MMM_SCAN_MAX)return ARTICLE_INVALID_QUERY;
    uint32_t boards[MMM_SCAN_MAX];
    for(unsigned i=0;i<list->count;i++) {
        const MmmBoardTarget *target=&list->targets[i];
        uint32_t board=target->board;
        if(!board) {
            result=find_label(target->label,&board);
            if(result!=ARTICLE_OK)return result;
        }
        ArticleBoardEntry entry;
        result=article_enter_board(actor,board,&entry);
        if(result!=ARTICLE_OK)return result;
        if(entry.password_board || entry.menu)return ARTICLE_BOARD_DENIED;
        for(unsigned j=0;j<i;j++)if(boards[j]==board)return ARTICLE_INVALID_QUERY;
        boards[i]=board;
    }
    return mmm_notes_scanlist_replace(actor->user,boards,list->count)?ARTICLE_OK:ARTICLE_SETTINGS_STORAGE;
}
