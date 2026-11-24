#define _POSIX_C_SOURCE 200809L
#include "session_internal.h"
#include "app_limits.h"
#include "date_filter.h"
#include "mail_query.h"
#include "profile_store.h"
#include "store_limits.h"
#include "vault_bridge.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/crypto.h>

typedef struct {
    char *text;
    size_t capacity, used;
    int failed;
} ViewOutput;

static void append(ViewOutput *out,const char *format,...) {
    if(out->failed)return;
    va_list args;
    va_start(args,format);
    int size=vsnprintf(out->text+out->used,out->capacity-out->used,format,args);
    va_end(args);
    if(size<0 || (size_t)size>=out->capacity-out->used)out->failed=1;
    else out->used+=(size_t)size;
}

static void quoted(ViewOutput *out,const char *str,size_t size) {
    append(out,"\"");
    for(size_t i=0;i<size && !out->failed;i++) {
        unsigned char ch=(unsigned char)str[i];
        if(ch=='"' || ch=='\\')append(out,"\\%c",ch);
        else if(ch<32)append(out,"\\u%04x",(unsigned)ch);
        else if(out->used+1>=out->capacity)out->failed=1;
        else out->text[out->used++]=(char)ch;
    }
    append(out,"\"");
}

static void string(ViewOutput *out,const char *str) {
    quoted(out,str,strlen(str));
}

static void count_text(ViewOutput *out,int known,uint32_t count) {
    if(known)append(out,"\"%u\"",count);
    else append(out,"\"-\"");
}

static void stamp_text(ViewOutput *out,uint64_t stamp) {
    char formatted[32]="unknown";
    time_t time=(time_t)stamp;
    struct tm civil;
#ifdef _WIN32
    int valid=time && localtime_s(&civil,&time)==0;
#else
    int valid=time && localtime_r(&time,&civil)!=NULL;
#endif
    if(valid)strftime(formatted,sizeof(formatted),date_pattern(),&civil);
    string(out,formatted);
}

static void render_page(ViewOutput *out,const OperationPage *page) {
    unsigned count=0;
    uint32_t next=0;
    const char *key=NULL;
    switch(page->kind) {
    case OPERATION_PAGE_BOARDS: key="boards";count=page->data.boards.count;next=page->data.boards.next;break;
    case OPERATION_PAGE_NOTES: key="notes";count=page->data.notes.count;next=page->data.notes.next;break;
    case OPERATION_PAGE_TREE: key="tree";count=page->data.tree.count;next=page->data.tree.next;break;
    case OPERATION_PAGE_MAIL: key="mail";count=page->data.mail.count;next=page->data.mail.next;break;
    case OPERATION_PAGE_FILES: key="files";count=(unsigned)page->data.files.count;next=page->data.files.next;break;
    case OPERATION_PAGE_PROFILE: {
        const MemberProfile *p=&page->data.profile;
        append(out,"\"profile\":{\"id\":%u,\"login\":",p->user);string(out,p->name);
        append(out,",\"fields\":{");
        for(unsigned i=0;i<PROFILE_FIELDS;i++) {
            const char *value=member_profile_value(p,i);
            if(i>=7 && !*value)continue;
            if(i)append(out,",");
            string(out,profile_key(i));append(out,":");string(out,value);
        }
        append(out,"}}");
        return;
    }
    default: return;
    }
    append(out,"\"next\":%u,\"%s\":[",next,key);
    for(unsigned i=0;i<count;i++) {
        append(out,"%s{",i?",":"");
        switch(page->kind) {
        case OPERATION_PAGE_BOARDS: {
            const ArticleBoardRow *r=&page->data.boards.rows[i];
            append(out,"\"id\":\"%u\",\"parent\":\"%u\",\"depth\":\"%u\",\"write\":\"%s\",\"unread\":",
                   r->id,r->parent,r->depth,r->writable?"yes":"no");
            count_text(out,r->unread_known,r->unread);
            append(out,",\"title\":");string(out,r->title);
            break;
        }
        case OPERATION_PAGE_NOTES: {
            const ArticleNoteRow *r=&page->data.notes.rows[i];
            append(out,"\"id\":\"%u\",\"replies\":\"%u\",\"latest\":\"%u\",\"updated\":",
                   r->summary.id,r->replies,r->latest);
            stamp_text(out,r->updated);
            append(out,",\"unread\":");count_text(out,page->data.notes.unread_known,r->unread);
            append(out,",\"title\":");string(out,r->summary.title);
            break;
        }
        case OPERATION_PAGE_TREE: {
            const ArticleTreeRow *r=&page->data.tree.rows[i];
            append(out,"\"id\":\"%u\",\"parent\":\"%u\",\"depth\":\"%u\",\"title\":",
                   r->summary.id,r->summary.parent,r->depth);
            string(out,r->summary.title);
            break;
        }
        case OPERATION_PAGE_MAIL: {
            const MailSummary *r=&page->data.mail.rows[i];
            append(out,"\"id\":\"%u\",\"read\":\"%d\",\"sender\":",r->id,r->read);
            string(out,r->from);append(out,",\"recipient\":");string(out,r->to);
            append(out,",\"title\":");string(out,r->title);
            break;
        }
        case OPERATION_PAGE_FILES: {
            const VaultEntry *r=&page->data.files.rows[i];
            append(out,"\"id\":%u,\"size\":%u,\"attribute\":\"%s\",\"downloads\":%llu,\"name\":",
                   r->id,r->size,r->ascii?"ASCII":"BINARY",(unsigned long long)page->data.files.downloads[i]);
            string(out,r->name);append(out,",\"description\":");string(out,r->description);
            break;
        }
        default: break;
        }
        append(out,"}");
    }
    append(out,"]");
}

static void render_content(ViewOutput *out,const OperationContent *r,const char *body) {
    if(r->kind==1) {
        char meta[48];
        snprintf(meta,sizeof(meta),"AUTHOR %s",*r->author?r->author:"guest");
        append(out,"\"article\":{\"id\":%u,\"parent\":%u,\"meta\":",r->id,r->parent);
        string(out,meta);
        append(out,",\"author\":");string(out,*r->author?r->author:"guest");
    } else {
        append(out,"\"message\":{\"id\":%u,\"parent\":%u,\"read\":%d,\"sender\":",r->id,r->parent,r->state);
        string(out,r->author);append(out,",\"recipient\":");string(out,r->recipient_name);
        append(out,",\"attachments\":[");
        for(unsigned i=0;i<r->file_count;i++) {
            const MailFileInfo *file=&r->files[i];
            append(out,"%s{\"id\":%u,\"size\":%u,\"name\":",i?",":"",file->id,file->size);
            string(out,file->name);append(out,"}");
        }
        append(out,"]");
    }
    append(out,",\"title\":");string(out,r->title);
    append(out,",\"body\":");quoted(out,body,r->size);
    append(out,"}");
}

static void reply(Session *s,const char *nonce,const char *text,SessionResultView view) {
    size_t body=view.content?view.content->size:0;
    if(body>CONTENT_BODY) { session_abort(s);return; }
    size_t capacity=32768+6*body;
    ViewOutput out={.text=malloc(capacity),.capacity=capacity};
    if(!out.text) { session_abort(s);return; }
    int content=view.content && (view.content->kind==2 || view.content->state==ARTICLE_VISIBLE);
    append(&out,"CONNECTOR VIEW %s {\"text\":",nonce);
    string(&out,(content || view.page)?"":text);
    append(&out,",\"data\":{");
    if(content)render_content(&out,view.content,view.body);
    else if(view.page)render_page(&out,view.page);
    append(&out,"}}\r\n");
    if(out.failed)session_abort(s);
    else session_impl_queue_text(s,out.text);
    OPENSSL_cleanse(out.text,out.used);
    free(out.text);
}

static int request(OperationRequest *q,const char *kind,uint32_t id,uint32_t cursor,const char *path) {
    if(!strcmp(kind,"files")) {
        q->op='Y';q->action=*path?VAULT_CMD_IN:VAULT_CMD_LIST;q->number=cursor?cursor:1;
        if(strlen(path)>48 || strchr(path,'|') || !session_impl_safe_text(path))return 0;
        strcpy(q->title,path);
    } else if(*path)return 0;
    else if(!strcmp(kind,"boards") || !strcmp(kind,"notes")) {
        q->op=kind[0]=='b'?'b':'O';q->number=cursor?cursor:1;
    } else if(!strcmp(kind,"mail")) {
        q->op='m';q->action='L';q->number=MAIL_LIST_ALL;q->cursor=cursor;
    } else {
        if(!id)return 0;
        q->number=id;
        if(!strcmp(kind,"read"))q->op='R';
        else if(!strcmp(kind,"readglobal")) { q->op='Z';q->action=5; }
        else if(!strcmp(kind,"tree")) { q->op='T';q->cursor=cursor; }
        else if(!strcmp(kind,"mail_read")) { q->op='m';q->action='R'; }
        else if(!strcmp(kind,"profile") && id<=MEMBER_MAX) { q->op='Z';q->action=7; }
        else return 0;
    }
    return 1;
}

void session_impl_web_view(Session *s,const char *payload) {
    char nonce[33],kind[16],number[11],start[11];
    int used=0;
    if(sscanf(payload,"%32[0-9a-f] %15[a-z_] %10[0-9] %10[0-9]%n",nonce,kind,number,start,&used)!=4 ||
       strlen(nonce)!=32 || !used || (payload[used] && payload[used]!=' '))
        return;
    SessionResultView empty={.status=SESSION_RESULT_FAILED};
    SessionRtbbsState *ui=rtbbs(s);
    if(ui->presentation.ui.enabled || session_secret(s) || ui->editor.editing ||
       ui->auth.application.step || ui->presentation.classic_input || ui->presentation.ui.input ||
       ui->mail_state.mail_input || ui->mail_state.mail.delete_id || ui->conversation.memo_input ||
       ui->conversation.talk_select || ui->conversation.chatting || ui->conversation.outside_talk ||
       ui->conversation.gather.mode || s->connection.binary || s->connection.zmodem || s->connection.door ||
       s->connection.connector_literal || s->connection.connector_block.data) {
        reply(s,nonce,"ERR connector busy\r\n",empty);return;
    }
    const char *path=payload+used+(payload[used]!=0);
    unsigned long id=strtoul(number,NULL,10),cursor=strtoul(start,NULL,10);
    OperationRequest *q=calloc(1,sizeof(*q));
    if(!q) { reply(s,nonce,"ERR board busy\r\n",empty);return; }
    if(id>CONTENT_ID_MAX || cursor>CONTENT_ID_MAX || !request(q,kind,(uint32_t)id,(uint32_t)cursor,path)) {
        free(q);reply(s,nonce,"ERR connector view syntax\r\n",empty);return;
    }
    if(!s->core.board_enabled && q->op!='m') {
        free(q);reply(s,nonce,"ERR no board\r\n",empty);return;
    }
    int structured=s->core.structured;
    q->board=s->core.board;s->core.structured=1;
    SessionOperationStatus status=session_submit(s,q);
    s->core.structured=structured;
    if(status==SESSION_OPERATION_ACCEPTED)memcpy(s->connection.connector_view_nonce,nonce,sizeof(nonce));
    else reply(s,nonce,status==SESSION_OPERATION_LOGIN_REQUIRED?"ERR login required\r\n":"ERR board busy\r\n",empty);
    free(q);
}

int session_impl_web_view_result(Session *s,const OperationResult *r,SessionResultView view) {
    char *nonce=s->connection.connector_view_nonce;
    if(!*nonce)return 0;
    if(view.status==SESSION_RESULT_OK && (r->mail_action=='L' || r->mail_action=='R')) {
        rtbbs(s)->mail_state.mail_mode=1;
        if(r->mail_action=='L')
            mail_navigation_filter(&rtbbs(s)->mail_state.mail,MAIL_LIST_ALL,NULL,r->mail_next);
    }
    reply(s,nonce,r->text,view);
    nonce[0]=0;
    return 1;
}
