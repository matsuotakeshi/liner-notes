#define _POSIX_C_SOURCE 200809L
#include "session_internal.h"
#include "chat.h"
#include "profile_store.h"
#include "runtime_limits.h"
#include "store_limits.h"
#include "vault_bridge.h"
#include "vault.h"
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void update_reply(Session *s,const char *nonce,const char *text,int ok,
                         uint32_t id,uint32_t revision,int audit_failed) {
    size_t size=strlen(text);
    char *record=malloc(128+2*size);
    if(!record) { session_abort(s);return; }
    size_t used=(size_t)sprintf(record,"CONNECTOR UPDATE %s %d %u %u %d ",
                                nonce,ok,id,revision,!!audit_failed);
    const char hex[]="0123456789abcdef";
    for(size_t i=0;i<size;i++) {
        unsigned char b=(unsigned char)text[i];
        record[used++]=hex[b>>4];record[used++]=hex[b&15];
    }
    memcpy(record+used,"\r\n",3);
    session_impl_queue_text(s,record);
    free(record);
}

static int number(const char *value,uint32_t *out) {
    if(!strcmp(value,"0")) { *out=0;return 1; }
    return session_impl_audit_number(value,out);
}

static int decode(const char *hex,char *out,size_t capacity) {
    const char digits[]="0123456789abcdef";
    size_t limit=strlen(hex);
    if(limit%2 || limit/2>=capacity)return 0;
    for(size_t i=0;i<limit;i+=2) {
        const char *high=strchr(digits,hex[i]),*low=strchr(digits,hex[i+1]);
        if(!high || !low || (high==digits && low==digits))return 0;
        out[i/2]=(char)(((high-digits)<<4)+(low-digits));
    }
    out[limit/2]=0;return 1;
}

static int text_valid(const char *text,int multiline) {
    const unsigned char *p=(const unsigned char *)text;
    size_t line=0,maximum=(size_t)ln_limit("INPUT",INPUT_LINE_MAX);
    while(*p) {
        if(multiline && p[0]=='\r' && p[1]=='\n') { line=0;p+=2;continue; }
        unsigned point=*p++,width=1;
        if(point>=128) {
            width=point>=0xc2 && point<=0xdf?2:point>=0xe0 && point<=0xef?3:
                  point>=0xf0 && point<=0xf4?4:0;
            if(!width)return 0;
            point&=(1u<<(7-width))-1;
            for(unsigned i=1;i<width;i++,p++) {
                if((*p&0xc0)!=0x80)return 0;
                point=(point<<6)|(*p&63);
            }
            if((width==2 && point<128) || (width==3 && point<2048) ||
               (width==4 && point<65536) || point>0x10ffff || (point>=0xd800 && point<=0xdfff))
                return 0;
        }
        if(point<32 || (point>=127 && point<=159) || (line+=width)>maximum)return 0;
    }
    return !multiline || !line;
}

static int attachments(char *csv,MailAttachments *out) {
    if(!*csv)return 1;
    for(;;) {
        char *next=strchr(csv,',');
        if(next)*next=0;
        uint32_t id;
        if(out->count==MAIL_ATTACH_MAX || !number(csv,&id) || !id || id>VAULT_ID_MAX ||
           mail_has(out,id))return 0;
        out->ids[out->count++]=id;
        if(!next)return 1;
        csv=next+1;
    }
}

static int conference_action(OperationRequest *q,const char *kind,uint32_t room,uint32_t cursor) {
    ConferenceRequest *request=&q->conference;
    size_t length=strlen(q->body);
    if(!strcmp(kind,"conference.seen")) {
        if(!cursor || length)return 0;
        request->action=CONFERENCE_SEEN;
    } else {
        if(!room || cursor || !length || length>CONFERENCE_TEXT)return 0;
        if(!strcmp(kind,"conference.post"))request->action=CONFERENCE_POST;
        else if(!strcmp(kind,"conference.create"))request->action=CONFERENCE_CREATE;
        else return 0;
        memcpy(request->text,q->body,length+1);
    }
    request->room=room;request->cursor=cursor;q->op=BOARD_CONFERENCE;
    return 1;
}

static int switch_value(const char *value) {
    if(!strcmp(value,"ON"))return 1;
    if(!strcmp(value,"OFF"))return 0;
    return -1;
}

/* Named form operations are mapped to the same worker requests as the terminal
 * UI. Target numbers are arguments only. Actor, epoch and grants come from Core. */
static int action(OperationRequest *q,const char *kind,uint32_t id,uint32_t arg,int confirmed) {
    const struct {
        const char *name;char op;int action,confirm,secondary;size_t limit;
    } actions[]={
        {"board.seen",'s',0,0,0,0},{"board.unseen",'u',0,0,0,0},
        {"board.retitle",'Z',2,0,0,BOARD_TITLE},{"board.withdraw",'Z',1,1,0,0},
        {"board.draft_delete",'X',0,1,0,0},{"board.get",'(','G',0,0,0},
        {"admin.hide",'h',0,1,0,0},{"admin.restore",'r',0,1,0,0},
        {"admin.author",'}','o',1,1,0},{"admin.move",'(','M',1,1,0},{"admin.move_branch",'(','B',1,1,0},
        {"admin.board_delete",'(','D',1,0,0},{"admin.board_delete_tree",'}','D',1,0,0},
        {"admin.board_guest",'}','p',0,0,3},{"admin.sigop_group",'}','G',1,1,0},
        {"admin.board_set",'!','b',0,0,1023},{"admin.board_options",'(','O',0,0,120},
        {"admin.board_pass_clear",'(','C',1,0,0},{"admin.board_group",'}','g',1,1,3},
        {"admin.sigop",'!','s',1,0,10},
        {"admin.approve",'%','A',1,0,0},{"admin.level",'%','L',1,0,1},
        {"admin.group",'%','G',0,1,0},{"admin.ungroup",'%','D',1,1,0},
        {"admin.group_move",')','G',1,1,0},
        {"admin.reject",BOARD_MEMBER_LIFECYCLE,1,1,0,0},
        {"admin.application_delete",BOARD_MEMBER_LIFECYCLE,2,1,0,0},
        {"admin.delete_user",BOARD_MEMBER_LIFECYCLE,4,1,0,0},
        {"admin.suspend",'x',0,1,0,0},{"admin.resume",'y',0,1,0,0},
        {"mail.delete",'m','D',1,0,0},{"mail.draft_delete",'m','C',1,0,0},{"mail.get",'}','M',0,0,0},
        {"mail.drop",'m','X',1,0,0},{"mail.notify",'m','U',0,0,3},
        {"file.desc",'Y',VAULT_CMD_DESCRIPTION,0,0,240},
        {"file.rename",'Y',VAULT_CMD_RENAME,0,0,100},
        {"file.category",'Y',VAULT_CMD_CATEGORY,0,0,48},
        {"file.ascii",'Y',VAULT_CMD_ASCII,0,0,3},
        {"file.copy",'Y',VAULT_CMD_COPY,0,0,48},
        {"file.delete",'Y',VAULT_CMD_DELETE,1,0,0},{"file.hide",'Y',VAULT_CMD_HIDE,1,0,0},
        {"file.restore",'Y',VAULT_CMD_RESTORE,1,0,0},{"admin.file_owner",'Y',VAULT_CMD_OWNER,1,1,0},
        {"admin.file_name",'Y',VAULT_CMD_BOARDNAME,0,0,80},
        {"admin.file_board",'Y',VAULT_CMD_BOARDSET,0,0,255},
        {"admin.file_options",'Y',VAULT_CMD_OPTIONS,0,0,110},
        {"admin.file_pass_clear",'Y',VAULT_CMD_PASSWORD,1,0,0},
        {"admin.file_sigop",'Y',VAULT_CMD_BOARDSIGOP,1,0,10},
        {"profile.set",')','F',0,0,320},{"profile.visibility",')','V',0,0,8},
        {"admin.profile",')','A',0,0,320},{"profile.handle",'Z',8,0,0,BOARD_TITLE},
        {"admin.application_set",'}','a',0,0,320},
        {"settings.buffer_clear",')','C',1,0,0},{"settings.save",'V',1,0,0,0},
    };
    for(size_t i=0;i<sizeof(actions)/sizeof(*actions);i++) {
        if(strcmp(kind,actions[i].name))continue;
        size_t size=strlen(q->body);
        if(size>actions[i].limit || !text_valid(q->body,0) || confirmed!=actions[i].confirm ||
           (!!arg!=actions[i].secondary))return 0;
        q->op=actions[i].op;q->action=actions[i].action;q->number=id;q->cursor=arg;
        if(q->op=='Y' && (q->action==VAULT_CMD_BOARDNAME || q->action==VAULT_CMD_OPTIONS ||
                          q->action==VAULT_CMD_PASSWORD || q->action==VAULT_CMD_BOARDSIGOP)) {
            size_t prefix=strlen(q->title);
            if(id || !prefix || prefix>48 || strchr(q->title,'|') || !text_valid(q->title,0))return 0;
            if(q->action==VAULT_CMD_BOARDSIGOP)
                return number(q->body,&q->cursor) && q->cursor<=MEMBER_MAX;
            memmove(q->body+prefix+1,q->body,size+1);
            memcpy(q->body,q->title,prefix);q->body[prefix]='|';
            return 1;
        }
        if((q->op==')' && strchr("FVA",q->action)) || (q->op=='}' && q->action=='a')) {
            if(q->op=='}' && (!id || id>MEMBER_MAX))return 0;
            if(q->op==')' && ((q->action=='A')!=!!id || (id && id>MEMBER_MAX)))return 0;
            for(char *p=q->title;*p;p++)if(*p>='a' && *p<='z')*p-=32;
            int field=profile_field(q->title);
            if(q->op=='}') {
                if(field<0)return !strcmp(q->title,"FROMBBS") || !strcmp(q->title,"SYSOPNOTE");
                q->op=')';q->action='A';
            }
            if(field<0)return 0;
            q->number=(uint32_t)field;q->cursor=id;
            if(q->action=='V') {
                if(!strcmp(q->body,"PRIVATE"))q->cursor=0;
                else if(!strcmp(q->body,"MEMBERS"))q->cursor=1;
                else if(!strcmp(q->body,"MONTHDAY"))q->cursor=2;
                else return 0;
            }
            return 1;
        }
        if(*q->title)return 0;
        if(q->op=='!' && q->action=='b')return !id && size!=0;
        if(q->op=='Y' && q->action==VAULT_CMD_BOARDSET)return !id && size!=0;
        if(q->op=='V' || (q->op==')' && q->action=='C') ||
           (q->op=='Z' && q->action==8) || (q->op=='m' && q->action=='U')) {
            if(id)return 0;
        } else if(!id || (id>CONTENT_ID_MAX && q->op!='X' && !(q->op=='m' && q->action=='C')))return 0;
        if(strchr("%xy{",q->op) && id>MEMBER_MAX)return 0;
        if((q->op=='%' && strchr("GD",q->action) && arg>100) ||
           (q->op=='}' && q->action=='G' && arg>100) ||
           (q->op==')' && q->action=='G' && (id>100 || arg>100)))return 0;
        if(q->op=='%' && q->action=='L') {
            if(!number(q->body,&q->cursor) || q->cursor>5)return 0;
        }
        if(q->op=='}' && q->action=='p') {
            int on=switch_value(q->body);if(on<0)return 0;q->cursor=(uint32_t)on;
        }
        if(q->op=='}' && q->action=='g' &&
           (arg>100 || !number(q->body,&q->audit_actor) || !q->audit_actor || q->audit_actor>100))return 0;
        if(q->op=='!' && q->action=='s' && (!number(q->body,&q->cursor) || q->cursor>MEMBER_MAX))return 0;
        if(q->op=='Y' && !size && (q->action==VAULT_CMD_CATEGORY ||
                                  q->action==VAULT_CMD_COPY || q->action==VAULT_CMD_RENAME))return 0;
        if(q->op=='X')q->cursor=id;
        if(q->op=='(' && (q->action=='M' || q->action=='B')) {
            q->number=q->board;q->cursor=id;q->audit_article=arg;
        }
        if(q->op=='m' && q->action=='C')q->mail_revision=id;
        if(q->op=='m' && q->action=='U') {
            int on=switch_value(q->body);if(on<0)return 0;q->number=on?1:2;
        }
        if(q->op=='Z') {
            if(q->action==1)q->cursor=1;
            else { if(q->action==2 && !size)return 0;strcpy(q->title,q->body); }
        }
        return 1;
    }
    return 0;
}

static int article(OperationRequest *q,uint32_t board,uint32_t parent,uint32_t revision,int draft) {
    if(q->mail_attachments.count || !board || parent>CONTENT_ID_MAX)return 0;
    q->board=board;q->number=parent;q->cursor=revision;
    q->op=draft?'w':'P';q->action='b';return 1;
}

static int compose(Session *s,OperationRequest *q,const char *kind,uint32_t board,uint32_t id,int draft) {
    if(!text_valid(q->body,1))return 0;
    if(!strcmp(kind,"gather")) {
        if(board || id || draft || *q->title || q->mail_attachments.count)return 0;
        q->op='!';q->action='g';return 1;
    }
    if(!*q->title || !text_valid(q->title,0))return 0;
    if(!strcmp(kind,"article"))
        return *q->body && article(q,board?board:s->core.board,id,0,draft);
    if(!strcmp(kind,"mail") || !strcmp(kind,"reply_mail")) {
        if(board || id>CONTENT_ID_MAX || (*q->username!=0)==(id!=0) || !*q->body ||
           (!strcmp(kind,"reply_mail"))!=!!id)return 0;
        if(strspn(q->username,"abcdefghijklmnopqrstuvwxyz0123456789_-")!=strlen(q->username))return 0;
        q->op='m';q->action=draft?'w':'s';q->number=id;return 1;
    }
    if(strcmp(kind,"editing") || board || id)return 0;
    SessionRtbbsState *ui=rtbbs(s);
    const OperationRequest *saved=&ui->editor.draft;
    if(saved->op=='m') {
        if(!*q->body)return 0;
        q->op='m';q->action=draft?'W':'S';q->number=saved->number;q->cursor=saved->cursor;
        q->mail_revision=ui->mail_state.mail_revision;
    } else if(saved->op=='?') {
        if(draft || q->mail_attachments.count || !*q->body)return 0;
        q->op='?';q->action='M';
    } else if(saved->op==')' || saved->op=='(' || saved->op=='Y') {
        if(q->mail_attachments.count)return 0;
        q->op=saved->op;q->action=saved->action;q->number=saved->number;
        if(saved->op=='Y') {
            size_t prefix=strlen(saved->title)+1,body=strlen(q->body);
            if(body>8192)return 0;
            memmove(q->body+prefix,q->body,body+1);
            memcpy(q->body,saved->title,prefix-1);q->body[prefix-1]='|';
        }
    } else if(!*q->body || !article(q,s->core.board,saved->number,ui->editor.draft_revision,draft))return 0;
    return 1;
}

void session_impl_web_update(Session *s,char *payload) {
    enum { NONCE,KIND,BOARD,ID,ARG,FLAG,ITEMS,NAME,TITLE,BODY,FIELDS };
    char *value[FIELDS]={payload};
    char *end=strchr(payload,'|');
    if(!end || end-payload!=32 || strspn(payload,"0123456789abcdef")!=32) { session_abort(s);return; }
    for(unsigned i=1;i<FIELDS;i++) {
        end=strchr(value[i-1],'|');
        if(!end) { update_reply(s,value[NONCE],"ERR connector update syntax\r\n",0,0,0,0);return; }
        *end=0;value[i]=end+1;
    }
    const char *nonce=value[NONCE],*kind=value[KIND];
    int editing=!strcmp(kind,"editing"),chatting=!strcmp(kind,"chat_send");
    if(!session_impl_web_idle(s,editing,chatting)) { update_reply(s,nonce,"ERR connector busy\r\n",0,0,0,0);return; }
    OperationRequest *q=calloc(1,sizeof(*q));
    if(!q) { update_reply(s,nonce,"ERR board busy\r\n",0,0,0,0);return; }
    uint32_t board,id,arg,flag;
    const char *error="ERR connector update syntax\r\n";
    q->board=s->core.board;
    if(!number(value[BOARD],&board) || board>=sizeof(q->board_grants)/sizeof(*q->board_grants) ||
       !number(value[ID],&id) || !number(value[ARG],&arg) || !number(value[FLAG],&flag) || flag>1 ||
       !attachments(value[ITEMS],&q->mail_attachments) ||
       !decode(value[NAME],q->username,sizeof(q->username)) ||
       !decode(value[TITLE],q->title,sizeof(q->title)) ||
       !decode(value[BODY],q->body,sizeof(q->body)))goto finish;
    if(chatting) {
        const SessionConversation *conversation=&rtbbs(s)->conversation;
        if(board || id || arg || flag || *q->title || *q->username || q->mail_attachments.count ||
           !chat_text_valid(q->body))goto finish;
        if(conversation->talk_lost || (conversation->outside_talk && !conversation->talk_target)) {
            error="CHAT ERROR target\r\n";goto finish;
        }
        q->op='&';q->chat_target=conversation->talk_target;q->action=q->chat_target?'D':'S';
    } else if(!strncmp(kind,"conference.",11)) {
        if(board || flag || *q->title || *q->username || q->mail_attachments.count ||
           !conference_action(q,kind,id,arg))goto finish;
    } else if(strchr(kind,'.')) {
        if(board || *q->username || q->mail_attachments.count || !action(q,kind,id,arg,(int)flag))goto finish;
    } else if(arg || (strcmp(kind,"mail") && *q->username) || !compose(s,q,kind,board,id,(int)flag))goto finish;
    if(!s->core.user && q->op!='P' && q->op!='?') { error="ERR login required\r\n";goto finish; }
    if(q->op=='?' && !sysop_mail_ready(&rtbbs(s)->conversation.sysop_mail,s->core.now)) {
        error="ERR try later\r\n";goto finish;
    }
    if(!s->core.board_enabled && (q->op=='P' || q->op=='w' || strchr("suZhrY(",q->op))) {
        error="ERR no board\r\n";goto finish;
    }
    if(q->op=='V') { q->number=1;session_impl_preferences_request(s,q); }
    SessionOperationStatus status=session_submit(s,q);
    if(status==SESSION_OPERATION_ACCEPTED) {
        memcpy(s->connection.connector_update_nonce,nonce,33);
        s->connection.connector_finish_editor=editing;
        if(q->op=='?')sysop_mail_submitted(&rtbbs(s)->conversation.sysop_mail,1);
        error=NULL;
    } else error=status==SESSION_OPERATION_LOGIN_REQUIRED?"ERR login required\r\n":"ERR board busy\r\n";
finish:
    OPENSSL_cleanse(q,sizeof(*q));free(q);
    if(error)update_reply(s,nonce,error,0,0,0,0);
}

int session_impl_web_update_result(Session *s,const OperationResult *r,SessionResultView result) {
    char *nonce=s->connection.connector_update_nonce;
    if(!*nonce)return 0;
    int ok=result.status==SESSION_RESULT_OK;
    SessionRtbbsState *ui=rtbbs(s);
    const char *text=r->text;
    if(ok && (r->chat_action=='S' || r->chat_action=='D')) {
        MessageId message;
        const char *error=session_impl_chat_delivery(s,r,s->core.now,&message);
        ok=!error;text=error?error:"";
    }
    if(ui->conversation.sysop_mail.pending)
        sysop_mail_complete(&ui->conversation.sysop_mail,ok && r->service_action=='M',s->core.now);
    if(ok && s->connection.connector_finish_editor) {
        OPENSSL_cleanse(&ui->editor.draft,sizeof(ui->editor.draft));
        ui->editor.editing=ui->editor.editor_input=ui->editor.editor_upload=0;
        ui->editor.editor_confirm=ui->editor.editor_upload_question=0;
        ui->editor.draft_revision=ui->mail_state.mail_revision=0;
        session_impl_undo_clear(s);
    }
    uint32_t id=r->mail_id?r->mail_id:r->selected;
    if(r->page.kind==OPERATION_PAGE_CONFERENCE) {
        const ConferenceReply *page=&r->page.data.conference;
        text=session_impl_conference_error(page->status);
        if(ok)id=page->action==CONFERENCE_CREATE?page->room:page->position;
    }
    update_reply(s,nonce,text,ok,id,
                 r->mail_revision?r->mail_revision:r->draft_revision,r->feedback.audit_failed);
    nonce[0]=0;s->connection.connector_finish_editor=0;
    return 1;
}
