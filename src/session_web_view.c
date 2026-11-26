#define _POSIX_C_SOURCE 200809L
#include "session_internal.h"
#include "app_limits.h"
#include "catalog.h"
#include "date_filter.h"
#include "mail_query.h"
#include "profile_store.h"
#include "store_limits.h"
#include "vault_bridge.h"
#include "traversal_protocol.h"
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

static void render_conference(ViewOutput *out,const ConferenceReply *page) {
    append(out,"\"conference\":{\"room\":%u,\"next\":%u,\"through\":%u,",page->room,page->next,page->position);
    if(page->action==CONFERENCE_LIST) {
        append(out,"\"rooms\":[");
        for(unsigned i=0;i<page->count;i++) {
            const ConferenceRoom *room=&page->rows.rooms[i];
            append(out,"%s{\"id\":%u,\"title\":",i?",":"",room->id);
            string(out,room->title);append(out,"}");
        }
    } else {
        append(out,"\"lines\":[");
        for(unsigned i=0;i<page->count;i++) {
            const ConferenceLine *line=&page->rows.lines[i];
            append(out,"%s{\"id\":%u,\"room\":%u,\"user\":%u,\"time\":",i?",":"",line->id,line->room,line->user);
            stamp_text(out,line->time);
            append(out,",\"name\":");string(out,line->name);
            append(out,",\"text\":");string(out,line->text);append(out,"}");
        }
    }
    append(out,"]}");
}

static void render_report(ViewOutput *out,const OperationPage *page) {
    const char *title,*columns;
    unsigned count;uint32_t next;
    switch(page->kind) {
    case OPERATION_PAGE_MEMBERS:
        title="MEMBERS";columns="\"user\",\"login\",\"status\"";
        count=page->data.members.count;next=page->data.members.next;break;
    case OPERATION_PAGE_ARTICLE_AUDIT:
        title="AUDIT";columns="\"id\",\"date\",\"user\",\"login\",\"event\",\"target\",\"board\"";
        count=page->data.article_audit.count;next=page->data.article_audit.next;break;
    case OPERATION_PAGE_FILE_AUDIT:
        title="FILEAUDIT";columns="\"id\",\"file\",\"date\",\"event\",\"user\",\"before\",\"after\"";
        count=(unsigned)page->data.file_audit.count;next=page->data.file_audit.next;break;
    case OPERATION_PAGE_DOWNLOAD_HISTORY:
        title="FILEHISTORY";columns="\"id\",\"file\",\"user\",\"timestamp\",\"bytes\",\"protocol\"";
        count=(unsigned)page->data.download_history.count;next=(uint32_t)page->data.download_history.next;break;
    case OPERATION_PAGE_DOWNLOAD_STATS:
        title="FILESTATS";columns="\"file\",\"downloads\"";
        count=(unsigned)page->data.download_stats.count;next=(uint32_t)page->data.download_stats.next;break;
    default:
        title="MAIL FILES";columns="\"file\",\"bytes\",\"name\",\"status\"";
        count=page->data.mail_files.count;next=page->data.mail_files.next;break;
    }
    append(out,"\"next\":%u,\"report\":{\"kind\":\"table\",\"title\":\"%s\",\"columns\":[%s],\"rows\":[",next,title,columns);
    for(unsigned i=0;i<count;i++) {
        append(out,"%s[",i?",":"");
        switch(page->kind) {
        case OPERATION_PAGE_MEMBERS: {
            const MemberSummary *r=&page->data.members.rows[i];
            append(out,"\"%u\",",r->id);string(out,r->name);
            append(out,",");string(out,r->state==MEMBER_STATE_PENDING?"pending":r->state==MEMBER_STATE_SUSPENDED?"suspended":"active");
            break;
        }
        case OPERATION_PAGE_ARTICLE_AUDIT: {
            const ArticleAuditRow *r=&page->data.article_audit.rows[i];
            static const char *const actions[]={"HIDE","RESTORE","WITHDRAW","RETITLE","AUTHOR","CHAT_KICK","SUSPEND","RESUME"};
            append(out,"\"%u\",",r->sequence);stamp_text(out,r->time);
            append(out,",\"%u\",",r->actor);string(out,r->actor_name);
            append(out,",");string(out,(unsigned)r->action<sizeof(actions)/sizeof(*actions)?actions[r->action]:"?");
            append(out,",\"%u\",\"%u\"",r->target,r->board);
            break;
        }
        case OPERATION_PAGE_FILE_AUDIT: {
            const VaultEvent *r=&page->data.file_audit.rows[i];
            char action[]={r->action,0};
            append(out,"\"%u\",\"%u\",",r->sequence,r->file);stamp_text(out,r->at);
            append(out,",");string(out,action);append(out,",");string(out,r->actor);
            append(out,",");string(out,r->before);append(out,",");string(out,r->after);
            break;
        }
        case OPERATION_PAGE_DOWNLOAD_HISTORY: {
            const DownloadHistoryRow *r=&page->data.download_history.rows[i];
            append(out,"\"%zu\",\"%u\",",r->sequence,r->file);string(out,r->subject);
            append(out,",\"%llu\",\"%u\",",(unsigned long long)r->at,r->size);string(out,r->protocol);
            break;
        }
        case OPERATION_PAGE_DOWNLOAD_STATS: {
            const DownloadCount *r=&page->data.download_stats.rows[i];
            append(out,"\"%u\",\"%llu\"",r->file,(unsigned long long)r->count);
            break;
        }
        default: {
            const MailFileInfo *r=&page->data.mail_files.rows[i];
            append(out,"\"%u\",\"%u\",",r->id,r->size);string(out,r->name);
            append(out,",");string(out,r->active?"active":"ready");
            break;
        }
        }
        append(out,"]");
    }
    append(out,"],\"next\":%u}",next);
}

static void render_page(ViewOutput *out,const OperationPage *page) {
    unsigned count=0;
    uint32_t next=0;
    const char *key=NULL;
    switch(page->kind) {
    case OPERATION_PAGE_BOARDS: key="boards";count=page->data.boards.count;next=page->data.boards.next;break;
    case OPERATION_PAGE_NOTES: key="notes";count=page->data.notes.count;next=page->data.notes.next;break;
    case OPERATION_PAGE_MEMBERS: case OPERATION_PAGE_ARTICLE_AUDIT:
    case OPERATION_PAGE_FILE_AUDIT: case OPERATION_PAGE_MAIL_FILES:
    case OPERATION_PAGE_DOWNLOAD_HISTORY: case OPERATION_PAGE_DOWNLOAD_STATS:
        render_report(out,page);return;
    case OPERATION_PAGE_UNREAD: key="matches";count=page->data.unread.count;next=page->data.unread.next;break;
    case OPERATION_PAGE_FILE_CHILDREN:
        key="file_boards";count=(unsigned)page->data.file_children.count;next=page->data.file_children.next;break;
    case OPERATION_PAGE_FILE_TEXT: {
        const OperationFileText *r=&page->data.file_text;
        append(out,"\"next\":%u,\"file_text\":{\"id\":%u,\"body\":",r->next,r->id);
        quoted(out,r->text,r->size);append(out,"}");return;
    }
    case OPERATION_PAGE_FILE_DETAIL: {
        const VaultQueryDetail *r=&page->data.file_detail;
        char digest[65];vault_hex(r->entry.sha256,digest);
        append(out,"\"report\":{\"kind\":\"fields\",\"title\":\"FILE\",\"fields\":[[\"file\",\"%u\"],[\"bytes\",\"%u\"],[\"sha256\",",
               r->entry.id,r->entry.size);string(out,digest);
        append(out,"],[\"name\",");string(out,r->entry.name);
        append(out,"],[\"description\",");string(out,r->entry.description);
        append(out,"],[\"publisher\",");string(out,r->entry.publisher);
        append(out,"],[\"category\",");string(out,*r->entry.category?r->entry.category:"-");
        append(out,"],[\"ASCII\",\"%s\"],[\"protocol\",",r->entry.ascii?"ON":"OFF");
        string(out,r->protocol);append(out,"]]}");return;
    }
    case OPERATION_PAGE_ARTICLES: key="matches";count=page->data.articles.count;next=page->data.articles.next;break;
    case OPERATION_PAGE_TREE: key="tree";count=page->data.tree.count;next=page->data.tree.next;break;
    case OPERATION_PAGE_MAIL: key="mail";count=page->data.mail.count;next=page->data.mail.next;break;
    case OPERATION_PAGE_PROFILES: key="profiles";count=page->data.profiles.count;next=page->data.profiles.next;break;
    case OPERATION_PAGE_FILE_BOARDS: key="file_boards";count=(unsigned)page->data.file_boards.count;next=page->data.file_boards.next;break;
    case OPERATION_PAGE_FILES: key="files";count=(unsigned)page->data.files.count;next=page->data.files.next;break;
    case OPERATION_PAGE_GATHER:
        key="gather";count=page->data.gather.count;next=page->data.gather.next;break;
    case OPERATION_PAGE_CONNECTIONS: {
        static const char *const modes[]={"BBS","EDITOR","CHAT","AUTH","BINARY","ZMODEM","DOOR"};
        append(out,"\"report\":{\"kind\":\"table\",\"title\":\"CONNECTIONS\",\"columns\":["
                   "\"connection\",\"login\",\"state\",\"transport\",\"peer\",\"seconds\",\"idle\","
                   "\"received\",\"sent\",\"queued\",\"columns\",\"rows\"],\"rows\":[");
        for(size_t i=0;i<page->data.connections.count;i++) {
            const ConnectionSummary *r=&page->data.connections.rows[i];
            append(out,"%s[\"%lu\",",i?",":"",r->id);string(out,r->login);
            append(out,",");string(out,(unsigned)r->mode<sizeof(modes)/sizeof(*modes)?modes[r->mode]:"?");
            append(out,",");string(out,r->telnet==2?"SSH":r->telnet?"TELNET":"RAW");
            append(out,",");string(out,*r->peer?r->peer:"unknown");
            append(out,",\"%llu\",\"%llu\",\"%llu\",\"%llu\",\"%zu\",\"%u\",\"%u\"]",
                   (unsigned long long)r->elapsed,(unsigned long long)r->idle,(unsigned long long)r->rx,
                   (unsigned long long)r->tx,r->queued,r->columns,r->rows);
        }
        append(out,"],\"next\":0}");return;
    }
    case OPERATION_PAGE_PREFERENCES: {
        const MemberPreferences *r=&page->data.preferences;
        append(out,"\"report\":{\"kind\":\"fields\",\"title\":\"PREFS\",\"fields\":[");
        if(r->present)append(out,"[\"language\",\"%s\"],[\"pager\",\"%s\"],[\"editor\",\"%s\"],"
                           "[\"columns\",\"%u\"],[\"rows\",\"%u\"]",
                           r->language?"en":"ja",r->pager?"ON":"OFF",r->editor?"LINE":"SOLID",r->columns,r->rows);
        else append(out,"[\"status\",\"NONE\"]");
        append(out,"]]}");return;
    }
    case OPERATION_PAGE_APPLICATION: {
        const MemberApplication *r=&page->data.application;
        append(out,"\"report\":{\"kind\":\"fields\",\"title\":\"APPLICATION\",\"fields\":["
                   "[\"user\",\"%u\"],[\"login\",",r->profile.user);string(out,r->profile.name);
        for(unsigned i=0;i<PROFILE_FIELDS;i++) {
            append(out,"],[");string(out,profile_key(i));append(out,",");
            string(out,member_profile_value(&r->profile,i));
        }
        const char *keys[]={"FROMBBS","SYSOPNOTE"};
        for(unsigned i=0;i<2;i++)if(r->notes.present[i]) {
            append(out,"],[");string(out,keys[i]);append(out,",");string(out,r->notes.values[i]);
        }
        append(out,"]]}");return;
    }
    case OPERATION_PAGE_CONFERENCE: render_conference(out,&page->data.conference);return;
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
        case OPERATION_PAGE_UNREAD:
        case OPERATION_PAGE_ARTICLES: {
            const ArticleSummary *r=page->kind==OPERATION_PAGE_UNREAD?
                &page->data.unread.rows[i].summary:&page->data.articles.rows[i];
            append(out,"\"id\":\"%u\",\"board\":\"%u\",\"parent\":\"%u\",\"title\":",
                   r->id,r->board,r->parent);
            string(out,r->title);
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
        case OPERATION_PAGE_PROFILES: {
            const MemberProfileRow *r=&page->data.profiles.rows[i];
            append(out,"\"id\":%u,\"login\":",r->user);string(out,r->name);
            append(out,",\"handle\":");string(out,r->handle);
            break;
        }
        case OPERATION_PAGE_GATHER: {
            const ActivityGather *r=&page->data.gather.rows[i];
            append(out,"\"id\":%u,\"date\":",r->sequence);stamp_text(out,r->time);
            append(out,",\"author\":");string(out,r->name);
            append(out,",\"handle\":");string(out,r->handle);
            append(out,",\"body\":");string(out,r->body);break;
        }
        case OPERATION_PAGE_FILE_CHILDREN: {
            const VaultQueryChild *r=&page->data.file_children.rows[i];
            append(out,"\"path\":");string(out,r->name);
            append(out,",\"title\":");string(out,*r->display?r->display:r->name);
            break;
        }
        case OPERATION_PAGE_FILE_BOARDS: {
            const VaultBoard *r=&page->data.file_boards.rows[i];
            append(out,"\"upload\":%s,\"path\":",r->can_upload?"true":"false");
            string(out,*r->name?r->name:"-");
            append(out,",\"title\":");string(out,*r->display?r->display:*r->name?r->name:"-");
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

static void view_reply(Session *s,const char *nonce,const char *text,SessionResultView view,const CheckTraversal *check) {
    size_t body=view.content?view.content->size:0;
    if(body>CONTENT_BODY) { session_abort(s);return; }
    size_t capacity=32768+6*body;
    if(view.page && view.page->kind==OPERATION_PAGE_FILE_TEXT)capacity+=6*view.page->data.file_text.size;
    if(view.page && view.page->kind==OPERATION_PAGE_CONNECTIONS)capacity+=32768;
    if(view.page && view.page->kind==OPERATION_PAGE_GATHER)
        capacity+=6*sizeof(view.page->data.gather);
    if(view.page && view.page->kind==OPERATION_PAGE_APPLICATION)
        capacity+=6*sizeof(view.page->data.application);
    ViewOutput out={.text=malloc(capacity),.capacity=capacity};
    if(!out.text) { session_abort(s);return; }
    int content=view.content && (view.content->kind==2 || view.content->state==ARTICLE_VISIBLE);
    append(&out,"CONNECTOR VIEW %s {\"text\":",nonce);
    string(&out,(content || view.page)?"":text);
    append(&out,",\"data\":{");
    if(content) {
        render_content(&out,view.content,view.body);
        if(view.sequence_next)append(&out,",\"next\":%u",view.sequence_next);
    }
    else if(view.page)render_page(&out,view.page);
    if(check)append(&out,"%s\"check\":{\"active\":%s,\"finished\":%s}",
                    content || view.page?",":"",check->active?"true":"false",check->finished?"true":"false");
    append(&out,"}}\r\n");
    if(out.failed)session_abort(s);
    else session_impl_queue_text(s,out.text);
    OPENSSL_cleanse(out.text,out.used);
    free(out.text);
}

static void reply(Session *s,const char *nonce,const char *text,SessionResultView view) {
    view_reply(s,nonce,text,view,NULL);
}

static SessionOperationStatus submit_view(Session *s,OperationRequest *q) {
    int structured=s->core.structured;
    if(!q->board)q->board=s->core.board;
    s->core.structured=1;
    SessionOperationStatus status=session_submit(s,q);
    s->core.structured=structured;
    return status;
}

static SessionOperationStatus submit_check_page(Session *s,OperationRequest *q,CheckTraversal *check) {
    q->since=check->since;q->until=check->until;q->cursor=check->cursor;q->board=check->board;
    if(check->active==1) { q->op='Z';q->action=11;q->scope=check->scope;strcpy(q->title,"CHECK"); }
    else if(check->active==2) { q->op='m';q->action='L';q->number=MAIL_LIST_NEWS;strcpy(q->title,"CHECK"); }
    else {
        q->op='Y';q->action=VAULT_CMD_NEWS;q->number=check->cursor;
        snprintf(q->body,sizeof(q->body),"%s%s@%llu:%llu",check->key,*check->key?"|":"",
                 (unsigned long long)check->since,(unsigned long long)check->until);
    }
    SessionOperationStatus status=submit_view(s,q);
    check->pending=status==SESSION_OPERATION_ACCEPTED;
    return status;
}

static void web_check(Session *s,const char *nonce,const char *kind,uint32_t board,const char *arg,
                      OperationRequest *q) {
    CheckTraversal *check=&s->connection.connector_check;
    const char *error="ERR connector check syntax\r\n";
    SessionOperationStatus status=SESSION_OPERATION_UNAVAILABLE;
    if(s->connection.connector_check_user!=s->core.user ||
       s->connection.connector_check_epoch!=s->core.user_epoch)
        check_traversal_reset(check);
    if(!s->core.user)error="ERR login required\r\n";
    else if(!strcmp(kind,"check_start") && board<=CATALOG_MAX && session_impl_safe_text(arg)) {
        CheckTraversal next={0};
        q->op='}';q->action='N';
        q->number=check_traversal_select(&next,arg,board?board:s->core.board);
        if(q->number<3) {
            strcpy(q->title,next.key);
            status=submit_view(s,q);
            if(status==SESSION_OPERATION_ACCEPTED) {
                *check=next;
                s->connection.connector_check_user=s->core.user;
                s->connection.connector_check_epoch=s->core.user_epoch;
            }
            error=NULL;
        }
    } else if(!board && !*arg) {
        if(!strcmp(kind,"check_cancel")) {
            check_traversal_reset(check);
            view_reply(s,nonce,"CHECK CANCELLED\r\n",(SessionResultView){0},check);
            return;
        }
        if(!strcmp(kind,"check_next")) {
            if(!check->active || check->finished)error="ERR no further CHECK page\r\n";
            else { status=submit_check_page(s,q,check);error=NULL; }
        } else if(!strcmp(kind,"check_done")) {
            CheckCommit commit;
            if(!check_traversal_commit(check,&commit))error="ERR finish CHECK NEXT first\r\n";
            else {
                q->op='}';q->action='n';q->number=commit.kind;q->until=commit.until;
                strcpy(q->title,commit.key);
                status=submit_view(s,q);
                check->committing=status==SESSION_OPERATION_ACCEPTED;
                error=NULL;
            }
        }
    }
    if(status==SESSION_OPERATION_ACCEPTED) {
        memcpy(s->connection.connector_view_nonce,nonce,33);
        s->connection.connector_check_request=1;
    } else view_reply(s,nonce,error?error:"ERR board busy\r\n",
                      (SessionResultView){.status=SESSION_RESULT_FAILED},check);
}

/* These views reuse the existing worker requests and their current actor ACLs. */
static int additional_request(OperationRequest *q,const char *kind,uint32_t id,uint32_t cursor,const char *arg) {
    if(!strcmp(kind,"article_scan") || !strcmp(kind,"mail_scan")) {
        if(!strncmp(arg,"DESC",4) && (!arg[4] || arg[4]==' '))q->descending=1;
        else if(strncmp(arg,"ASC",3) || (arg[3] && arg[3]!=' '))return 0;
        const char *space=strchr(arg,' ');
        q->cursor=cursor?cursor:q->descending?UINT32_MAX:1;
        if(!strcmp(kind,"mail_scan")) {
            if(space || id)return 0;
            q->op='m';q->action='f';
        } else {
            if(id>CATALOG_MAX || !space || !session_impl_safe_text(space+1) ||
               !session_impl_find_arguments(space+1,q))return 0;
            q->op='Z';q->action=4;q->board=id;
        }
        return 1;
    }
    if(!strcmp(kind,"file_children")) {
        if(id || !*arg || strlen(arg)>48 || strchr(arg,'|') || !session_impl_safe_text(arg))return 0;
        q->op='Y';q->action=VAULT_CMD_CHILDREN;q->number=cursor;
        strcpy(q->title,arg);return 1;
    }
    if(!strcmp(kind,"article_audit")) {
        if(strlen(arg)>32 || strchr(arg,' ') || !session_impl_safe_text(arg) || (id && *arg))return 0;
        q->op='A';q->audit_article=id;q->number=cursor;
        strcpy(q->title,arg);return 1;
    }
    if(!strcmp(kind,"profile_login")) {
        if(id || cursor || strlen(arg)>32 || strchr(arg,' ') || !session_impl_safe_text(arg))return 0;
        q->op='Z';q->action=7;strcpy(q->username,arg);return 1;
    }
    if(*arg)return 0;
    if(!strcmp(kind,"connections") && !id && !cursor) { q->op='!';q->action='L'; }
    else if(!strcmp(kind,"preferences") && !id && !cursor)q->op='V';
    else if(!strcmp(kind,"application") && id && id<=MEMBER_MAX && !cursor) { q->op='}';q->action='A';q->number=id; }
    else if((!strcmp(kind,"file_history") || !strcmp(kind,"file_stats")) && !id) {
        q->op='Y';q->action=!strcmp(kind,"file_history")?VAULT_CMD_HISTORY:VAULT_CMD_STATS;
        snprintf(q->title,sizeof(q->title),"%u",cursor);
    }
    else if(!strcmp(kind,"file_audit") && !id) { q->op='Y';q->action=VAULT_CMD_AUDIT;q->number=cursor; }
    else if(!strcmp(kind,"file_text") && id) {
        q->op='Y';q->action=VAULT_CMD_TEXT;q->number=id;
        snprintf(q->body,sizeof(q->body),"%u",cursor);
    }
    else if(!strcmp(kind,"file_detail") && id && !cursor) { q->op='Y';q->action=VAULT_CMD_INFO;q->number=id; }
    else if(!strcmp(kind,"gather") && !id) { q->op='!';q->action='G';q->cursor=cursor; }
    else if(!strcmp(kind,"mail_files") && !id) { q->op='m';q->action='I';q->cursor=cursor; }
    else if(!strcmp(kind,"member_list") && !id && cursor<=MEMBER_MAX) { q->op='l';q->number=cursor; }
    else if(!strcmp(kind,"applications") && !id && cursor<=MEMBER_MAX) { q->op='%';q->action='P';q->cursor=cursor; }
    else if(!strcmp(kind,"article_unread") && id<=CATALOG_MAX) { q->op='v';q->board=id;q->cursor=cursor; }
    else if(!strcmp(kind,"article_new") && id<=CATALOG_MAX && !cursor) { q->op='n';q->board=id; }
    else if(!strcmp(kind,"mail_next") && !id && !cursor) { q->op='m';q->action='N'; }
    else if(!strcmp(kind,"mail_previous") && id>1 && !cursor) { q->op='m';q->action='f';q->cursor=id-1;q->descending=1; }
    else if(!strcmp(kind,"article_prev") && id && !cursor) { q->op='B';q->number=id; }
    else if(!strcmp(kind,"article_next") && id && !cursor) { q->op='N';q->number=id; }
    else if(!strcmp(kind,"article_parent") && id && !cursor) { q->op='U';q->number=id; }
    else if(!strcmp(kind,"article_child") && id && !cursor) { q->op='C';q->number=id; }
    else return 0;
    return 1;
}

static int request(OperationRequest *q,const char *kind,uint32_t id,uint32_t cursor,const char *path) {
    if(additional_request(q,kind,id,cursor,path))return 1;
    if(!strcmp(kind,"files")) {
        q->op='Y';q->action=*path?VAULT_CMD_IN:VAULT_CMD_LIST;q->number=cursor?cursor:1;
        if(strlen(path)>48 || strchr(path,'|') || !session_impl_safe_text(path))return 0;
        strcpy(q->title,path);
    } else if(!strcmp(kind,"file_search") || !strcmp(kind,"file_news")) {
        const char *bar=strchr(path,'|'),*filter=bar?bar+1:path;
        int news=!strcmp(kind,"file_news");
        uint64_t stamp;
        if(id || !*filter || strlen(filter)>80 || !session_impl_safe_text(path) ||
           (bar && (bar==path || bar-path>48 || strchr(filter,'|'))) ||
           (news && !date_filter(filter,&stamp)))return 0;
        q->op='Y';q->action=news?VAULT_CMD_NEWS:VAULT_CMD_SEARCH;
        q->number=cursor?cursor:1;
        strcpy(q->body,path);
    } else if(!strcmp(kind,"file_boards")) {
        if(id || *path)return 0;
        q->op='Y';q->action=VAULT_CMD_BOARDS;q->number=cursor;
    } else if(!strcmp(kind,"board_news")) {
        uint64_t stamp;
        if(id>CATALOG_MAX || !date_filter(path,&stamp))return 0;
        q->op='Z';q->action=11;q->board=id;q->cursor=cursor?cursor:1;
        strcpy(q->title,path);
    } else if(!strcmp(kind,"profiles") || !strcmp(kind,"profile_group") || !strcmp(kind,"profile_find")) {
        const char *query=path;
        if(cursor>MEMBER_MAX || !session_impl_safe_text(path))return 0;
        q->op='Z';q->action=10;q->cursor=cursor?cursor:1;
        if(!strcmp(kind,"profile_group")) {
            if(!id || id>100 || *path)return 0;
            q->scope=100;q->number=id;query="*";
        } else if(id)return 0;
        else if(!strcmp(kind,"profile_find")) {
            const char *space=strchr(path,' ');char field[32]={0};
            if(!space || space==path || (size_t)(space-path)>=sizeof(field))return 0;
            memcpy(field,path,(size_t)(space-path));
            int index=profile_field(field);
            if(index<0)return 0;
            q->scope=index+1;query=space+1;
        }
        if(!*query || strlen(query)>80)return 0;
        strcpy(q->title,query);
    } else if(!strcmp(kind,"find")) {
        q->op='Z';q->action=3;q->board=id;q->cursor=cursor?cursor:1;
        if(id>CATALOG_MAX || !session_impl_safe_text(path) || !session_impl_find_arguments(path,q))return 0;
    } else if(!strcmp(kind,"mail_search")) {
        if(id || !*path || strlen(path)>80 || !session_impl_safe_text(path))return 0;
        q->op='m';q->action='L';q->number=MAIL_LIST_SEARCH;q->cursor=cursor;
        strcpy(q->title,path);
    } else if(!strcmp(kind,"mail_news")) {
        uint64_t stamp;
        if(id || !date_filter(path,&stamp))return 0;
        q->op='m';q->action='L';q->number=MAIL_LIST_NEWS;q->cursor=cursor;
        strcpy(q->title,path);
    } else if(*path)return 0;
    else if(!strncmp(kind,"conf_",5)) {
        q->op=BOARD_CONFERENCE;q->conference.room=id;q->conference.cursor=cursor;
        if(!strcmp(kind,"conf_rooms") && !id)q->conference.action=CONFERENCE_LIST;
        else if(!strcmp(kind,"conf_history") && id)q->conference.action=CONFERENCE_HISTORY;
        else if(!strcmp(kind,"conf_unread"))q->conference.action=CONFERENCE_UNREAD;
        else if(!strcmp(kind,"conf_position") && id && !cursor)q->conference.action=CONFERENCE_POSITION;
        else return 0;
    } else if(!strcmp(kind,"boards") || !strcmp(kind,"notes")) {
        q->op=kind[0]=='b'?'b':'O';q->number=cursor?cursor:1;
    } else if(!strcmp(kind,"mail") || !strcmp(kind,"mail_inbox") ||
              !strcmp(kind,"mail_sent") || !strcmp(kind,"mail_unread")) {
        q->op='m';q->action='L';q->cursor=cursor;
        q->number=!strcmp(kind,"mail_inbox")?MAIL_LIST_INBOX:
                  !strcmp(kind,"mail_sent")?MAIL_LIST_SENT:
                  !strcmp(kind,"mail_unread")?MAIL_LIST_UNREAD:MAIL_LIST_ALL;
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

int session_impl_web_idle(Session *s,int editing,int chatting) {
    SessionRtbbsState *ui=rtbbs(s);
    return !ui->presentation.ui.enabled && !session_secret(s) &&
        !!ui->editor.editing==editing && !ui->auth.application.step &&
        !ui->presentation.classic_input && !ui->presentation.ui.input &&
        !ui->mail_state.mail_input && !ui->mail_state.mail.delete_id &&
        !ui->conversation.memo_input && !ui->conversation.talk_select &&
        !!ui->conversation.chatting==chatting && (chatting || !ui->conversation.outside_talk) &&
        !ui->conversation.gather.mode && !s->connection.binary &&
        !s->connection.zmodem && !s->connection.door &&
        !s->connection.connector_literal && !s->connection.connector_block.data;
}

void session_impl_web_view(Session *s,const char *payload) {
    char nonce[33],kind[16],number[11],start[11];
    int used=0;
    if(sscanf(payload,"%32[0-9a-f] %15[a-z_] %10[0-9] %10[0-9]%n",nonce,kind,number,start,&used)!=4 ||
       strlen(nonce)!=32 || !used || (payload[used] && payload[used]!=' '))
        return;
    SessionResultView empty={.status=SESSION_RESULT_FAILED};
    if(!session_impl_web_idle(s,0,0)) {
        reply(s,nonce,"ERR connector busy\r\n",empty);return;
    }
    const char *path=payload+used+(payload[used]!=0);
    unsigned long id=strtoul(number,NULL,10),cursor=strtoul(start,NULL,10);
    OperationRequest *q=calloc(1,sizeof(*q));
    if(!q) { reply(s,nonce,"ERR board busy\r\n",empty);return; }
    if(id<=CONTENT_ID_MAX && !cursor && !strncmp(kind,"check_",6)) {
        web_check(s,nonce,kind,(uint32_t)id,path,q);free(q);return;
    }
    if(id>CONTENT_ID_MAX || cursor>CONTENT_ID_MAX || !request(q,kind,(uint32_t)id,(uint32_t)cursor,path)) {
        free(q);reply(s,nonce,"ERR connector view syntax\r\n",empty);return;
    }
    if(!s->core.board_enabled && q->op!='m' && q->op!='%' && q->op!='l' && q->op!='!' && q->op!=BOARD_CONFERENCE) {
        free(q);reply(s,nonce,"ERR no board\r\n",empty);return;
    }
    SessionOperationStatus status=submit_view(s,q);
    if(status==SESSION_OPERATION_ACCEPTED)memcpy(s->connection.connector_view_nonce,nonce,sizeof(nonce));
    else reply(s,nonce,status==SESSION_OPERATION_LOGIN_REQUIRED?"ERR login required\r\n":"ERR board busy\r\n",empty);
    free(q);
}

int session_impl_web_view_result(Session *s,const OperationResult *r,SessionResultView view) {
    char *nonce=s->connection.connector_view_nonce;
    if(!*nonce)return 0;
    if(s->connection.connector_check_request) {
        CheckTraversal *check=&s->connection.connector_check;
        const char *text=r->text;
        if(view.status==SESSION_RESULT_OK && r->service_action=='n') {
            check_traversal_begin(check,r->service_number,r->since,r->until);
            OperationRequest *q=calloc(1,sizeof(*q));
            SessionOperationStatus status=q?submit_check_page(s,q,check):SESSION_OPERATION_UNAVAILABLE;
            free(q);
            if(status==SESSION_OPERATION_ACCEPTED)return 1;
            text="ERR board busy\r\n";
        } else if(check->committing)check_traversal_committed(check,r->feedback.checkpoint_saved);
        else if(check->pending) {
            if(r->feedback.page_found)check_traversal_page(check,1,r->feedback.page_next);
            else check->pending=0;
        }
        view_reply(s,nonce,text,view,check);
        s->connection.connector_check_request=0;nonce[0]=0;
        return 1;
    }
    if(view.status==SESSION_RESULT_OK && (r->mail_action=='L' || r->mail_action=='R')) {
        rtbbs(s)->mail_state.mail_mode=1;
        if(r->mail_action=='L')
            mail_navigation_filter(&rtbbs(s)->mail_state.mail,MAIL_LIST_ALL,NULL,r->mail_next);
    }
    const char *text=r->page.kind==OPERATION_PAGE_CONFERENCE?
        session_impl_conference_error(r->page.data.conference.status):r->text;
    reply(s,nonce,text,view);
    nonce[0]=0;
    return 1;
}
