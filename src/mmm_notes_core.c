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

ArticleOutcome mmm_signature_get(const ArticleActor *actor,MmmSignature *out) {
    memset(out,0,sizeof(*out));
    ArticleOutcome status=authorize(actor);
    if(status!=ARTICLE_OK)return status;
    return mmm_signature_load(actor->user,out)?ARTICLE_OK:ARTICLE_SETTINGS_STORAGE;
}

ArticleOutcome mmm_signature_set(const ArticleActor *actor,int action,const char *text,MmmSignature *out) {
    ArticleOutcome status=mmm_signature_get(actor,out);
    if(status!=ARTICLE_OK)return status;
    if(action=='g') {
        size_t size=strlen(text);
        if(size>MMM_SIGNATURE_MAX)return ARTICLE_INVALID_QUERY;
        for(size_t i=0;i<size;i++) {
            unsigned char c=(unsigned char)text[i];
            if(c=='\r' && i+1<size && text[i+1]=='\n')i++;
            else if(c<32 || c==127)return ARTICLE_INVALID_QUERY;
        }
        if(size && (size<2 || memcmp(text+size-2,"\r\n",2)))return ARTICLE_INVALID_QUERY;
        strcpy(out->body,text);
        if(!size)out->automatic=0;
    } else if(action=='a' && *out->body)out->automatic=1;
    else if(action=='n')out->automatic=0;
    else return ARTICLE_INVALID_QUERY;
    return mmm_signature_store(actor->user,out)?ARTICLE_OK:ARTICLE_SETTINGS_STORAGE;
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

static int match_pattern(const char *pattern,const char *text) {
    const char *star=NULL,*retry=NULL;
    while(*text) {
        if(*pattern=='?' || *pattern==*text) { pattern++;text++; }
        else if(*pattern=='*') { star=pattern++;retry=text; }
        else if(star) { pattern=star+1;text=++retry; }
        else return 0;
    }
    while(*pattern=='*')pattern++;
    return !*pattern;
}

static int glob_label(const char *text) { return strpbrk(text,"*?")!=NULL; }

static int lower_pattern(char *pattern) {
    for(char *p=pattern;*p;p++) {
        if(*p>='A' && *p<='Z')*p=(char)(*p+'a'-'A');
        if(!((*p>='a' && *p<='z') || (*p>='0' && *p<='9') || strchr("*?_.-",*p)))return 0;
    }
    return *pattern!=0;
}

static ArticleOutcome expand_rules(const ArticleActor *actor,const MmmScanTargets *rules,MmmScanList *out) {
    char labels[CATALOG_MAX+1][MMM_LABEL_MAX+1]={0};
    unsigned char used[CATALOG_MAX+1]={0};
    int labelled=0;
    for(unsigned i=0;i<rules->count;i++)
        if(!rules->targets[i].board && strcmp(rules->targets[i].label,"*"))labelled=1;
    if(labelled)for(uint32_t id=1;id<=CATALOG_MAX;id++)
        if(catalog_exists(id) && !mmm_notes_label_get(id,labels[id]))return ARTICLE_SETTINGS_STORAGE;

    out->custom=1;
    for(unsigned i=0;i<rules->count;i++) {
        const MmmBoardTarget *rule=&rules->targets[i];
        if(!rule->exclude)continue;
        for(uint32_t id=rule->board?rule->board:1;id<=(rule->board?rule->board:CATALOG_MAX);id++)
            if(rule->board || match_pattern(rule->label,labels[id]))used[id]=1;
    }
    for(unsigned i=0;i<rules->count;i++) {
        const MmmBoardTarget *rule=&rules->targets[i];
        if(rule->exclude)continue;
        for(uint32_t id=rule->board?rule->board:1;id<=(rule->board?rule->board:CATALOG_MAX);id++) {
            if(used[id] || (!rule->board && !match_pattern(rule->label,labels[id])))continue;
            used[id]=1;
            ArticleBoardEntry entry;
            ArticleOutcome result=article_enter_board(actor,id,&entry);
            if(result==ARTICLE_BOARD_DENIED || (result==ARTICLE_OK && (entry.password_board || entry.menu)))continue;
            if(result!=ARTICLE_OK)return result;
            if(out->count==MMM_SCAN_MAX)return ARTICLE_INVALID_QUERY;
            MmmScanBoard *row=&out->rows[out->count++];
            if(!mmm_notes_label_get(id,row->label))return ARTICLE_SETTINGS_STORAGE;
            row->board=id;row->mode=rule->mode;strcpy(row->title,entry.title);
        }
    }
    return ARTICLE_OK;
}

ArticleOutcome mmm_note_scanlist(const ArticleActor *actor,MmmScanList *out) {
    memset(out,0,sizeof(*out));
    ArticleOutcome result=authorize(actor);
    if(result!=ARTICLE_OK)return result;
    MmmScanTargets rules;
    if(!mmm_notes_scan_rules_load(actor->user,&rules))return ARTICLE_SETTINGS_STORAGE;
    if(rules.count) {
        result=expand_rules(actor,&rules,out);
        if(result!=ARTICLE_OK)memset(out,0,sizeof(*out));
        return result;
    }
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
    MmmScanTargets rules=*list;
    int enhanced=0;
    for(unsigned i=0;i<rules.count;i++) {
        MmmBoardTarget *target=&rules.targets[i];
        enhanced|=target->exclude || target->mode!=MMM_SCAN_NEW;
        if(!target->board && glob_label(target->label)) {
            if(!lower_pattern(target->label))return ARTICLE_INVALID_QUERY;
            enhanced=1;continue;
        }
        uint32_t board=target->board;
        if(!board) {
            result=find_label(target->label,&board);
            if(result!=ARTICLE_OK)return result;
            target->board=board;target->label[0]=0;
        }
        ArticleBoardEntry entry;
        result=article_enter_board(actor,board,&entry);
        if(result!=ARTICLE_OK)return result;
        if(entry.password_board || entry.menu)return ARTICLE_BOARD_DENIED;
        boards[i]=board;
    }
    if(enhanced) {
        MmmScanList expanded={0};
        result=expand_rules(actor,&rules,&expanded);
        if(result!=ARTICLE_OK)return result;
        for(unsigned i=0;i<expanded.count;i++)boards[i]=expanded.rows[i].board;
        return mmm_notes_scanlist_replace(actor->user,boards,expanded.count,&rules)?ARTICLE_OK:ARTICLE_SETTINGS_STORAGE;
    }
    for(unsigned i=0;i<list->count;i++)for(unsigned j=0;j<i;j++)
        if(boards[j]==boards[i])return ARTICLE_INVALID_QUERY;
    return mmm_notes_scanlist_replace(actor->user,boards,list->count,NULL)?ARTICLE_OK:ARTICLE_SETTINGS_STORAGE;
}

ArticleOutcome flat_scanlist(const ArticleActor *actor,MmmScanList *out) {
    memset(out,0,sizeof(*out));
    ArticleOutcome result=authorize(actor);
    if(result!=ARTICLE_OK)return result;
    uint32_t boards[MMM_SCAN_MAX];unsigned count;
    if(!big_model_scanlist_load(actor->user,boards,&count))return ARTICLE_SETTINGS_STORAGE;
    out->custom=count!=0;
    for(unsigned i=0;i<count;i++) {
        ArticleBoardEntry entry;
        result=article_enter_board(actor,boards[i],&entry);
        if(result==ARTICLE_BOARD_DENIED || (result==ARTICLE_OK && (entry.password_board || entry.menu)))continue;
        if(result!=ARTICLE_OK) { memset(out,0,sizeof(*out));return result; }
        MmmScanBoard *row=&out->rows[out->count++];
        row->board=entry.id;strcpy(row->title,entry.title);
    }
    return ARTICLE_OK;
}

ArticleOutcome flat_scanlist_save(const ArticleActor *actor,const MmmScanTargets *list) {
    ArticleOutcome result=authorize(actor);
    if(result!=ARTICLE_OK)return result;
    if(!list || list->count>MMM_SCAN_MAX)return ARTICLE_INVALID_QUERY;
    uint32_t boards[MMM_SCAN_MAX];
    for(unsigned i=0;i<list->count;i++) {
        const MmmBoardTarget *target=&list->targets[i];
        if(!target->board || *target->label || target->exclude || target->mode!=MMM_SCAN_NEW)
            return ARTICLE_INVALID_QUERY;
        for(unsigned j=0;j<i;j++)if(boards[j]==target->board)return ARTICLE_INVALID_QUERY;
        ArticleBoardEntry entry;
        result=article_enter_board(actor,target->board,&entry);
        if(result!=ARTICLE_OK)return result;
        if(entry.menu || entry.password_board)return ARTICLE_BOARD_DENIED;
        boards[i]=target->board;
    }
    return big_model_scanlist_replace(actor->user,boards,list->count)?ARTICLE_OK:ARTICLE_SETTINGS_STORAGE;
}
