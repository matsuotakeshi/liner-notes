/* Menu-only RTBBS bindings. Named commands still go through normal validation. */
static void command(Session *,char *);

static void classic_run(Session *s,const char *text) {
    char line[INPUT_LINE_MAX+128];snprintf(line,sizeof(line),"%s",text);command(s,line);
}
static void classic_ask(Session *s,int input,MessageId message) {
    s->classic_input=input;queue_text(s,ui_text(&s->ui,message));
}
static void classic_read(Session *s,int descending,uint32_t distance,int repeat) {
    uint32_t current=s->mail_mode?s->mail_current:s->current;
    if(!current) { emit(s,"ERR no current article\r\n");prompt(s);return; }
    BoardRequest q={0};q.op=s->mail_mode?'m':'Z';q.action=s->mail_mode?'f':4;q.descending=descending;
    if(s->mail_mode)q.scope=(int)s->ui.language+1;
    strcpy(q.username,"*");strcpy(q.title,"*");
    q.cursor=repeat?current:descending?(current>distance?current-distance:0):
        distance<=CONTENT_ID_MAX-current?current+distance:0;
    if(!q.cursor) { emit(s,"SCAN END\r\n");prompt(s);return; }
    s->classic_direction=descending;submit(s,&q);if(s->pending) { s->scan=q;s->scanning=1; }
}
static const char *classic_fields[]={"HANDLE","BIO","LOCATION","OCCUPATION","INTERESTS","WEBSITE","CONTACT","MACHINE","BIRTHDAY","REALNAME","KANA","ADDRESS","PHONE","GROUP"};
static void profile_fields(Session *s,int search) {
    char text[96];for(unsigned i=0;i<(search?14u:13u);i++) { snprintf(text,sizeof(text),"%u: %s\r\n",i+1,classic_fields[i]);queue_text(s,text); }
    s->classic_input=search?CI_SEARCH_FIELD:CI_PROF_FIELD;queue_text(s,s->ui.language==LANGUAGE_JA?"項目番号 (0:戻る) > ":"Field (0:back) > ");
}
static int classic_input(Session *s,const char *line) {
    int mode=s->classic_input;if(!mode)return 0;
    char text[INPUT_LINE_MAX+128];s->classic_input=0;
    if(!strcmp(line,"/CANCEL")) { s->batch_screen=0;s->file_selected=0;s->profile_walk=0;emit(s,"CANCELLED\r\n");prompt(s);return 1; }
    if(mode==CI_PROF_TARGET) {
        const char *end;uint32_t target=0;
        if(*line && (!number_arg(line,&target,&end) || *end)) { s->classic_input=CI_PROF_TARGET;queue_text(s,"Member ID (Enter:self) > ");return 1; }
        s->profile_edit_target=target;profile_fields(s,0);return 1;
    }
    if(mode==CI_PROF_NEXT) {
        if(!strcmp(line,"0") || !strcmp(line,"N")) { s->profile_walk=0;prompt(s);return 1; }
        s->profile_walk_pending=1;submit(s,&s->profile_search);return 1;
    }
    if(mode==CI_BATCH_COPY) {
        snprintf(text,sizeof(text),"BATCH COPY %s",*line?line:(*s->vault_path?s->vault_path:"-"));classic_run(s,text);return 1;
    }
    if(mode==CI_FILE_ACTION) {
        if(!strcmp(line,"0")) { s->file_selected=0;prompt(s);return 1; }
        if(!strcmp(line,"E")) { s->classic_input=CI_FILE_DESC;queue_text(s,s->ui.language==LANGUAGE_JA?"説明 > ":"Description > ");return 1; }
        if(!strcmp(line,"K")) { s->classic_input=CI_FILE_DELETE;queue_text(s,"DELETE? Y/N > ");return 1; }
        if(!strcmp(line,"B"))snprintf(text,sizeof(text),"BATCH ADD %u",s->file_selected);
        else if(!strcmp(line,"T"))snprintf(text,sizeof(text),"FILETYPE %u",s->file_selected);
        else { const char *keys="SCXYGZM";const char *key=strchr(keys,*line);if(!*line || line[1] || !key) { s->classic_input=CI_FILE_ACTION;prompt(s);return 1; }
            const unsigned modes[]={6,1,7,2,3,0,4};s->transfer_protocol=modes[key-keys];snprintf(text,sizeof(text),"DOWNLOAD %u",s->file_selected); }
        classic_run(s,text);return 1;
    }
    if(mode==CI_FILE_DESC) { snprintf(text,sizeof(text),"FILEDESC %u %s",s->file_selected,line);classic_run(s,text);return 1; }
    if(mode==CI_FILE_DELETE) { if(!strcmp(line,"Y") || !strcmp(line,"y")) { snprintf(text,sizeof(text),"FILEDELETE %u CONFIRM",s->file_selected);classic_run(s,text); }else prompt(s);return 1; }
    if(mode==CI_PROF_FIELD || mode==CI_SEARCH_FIELD) {
        if(!strcmp(line,"0")) { prompt(s);return 1; }uint32_t field;const char *end;
        if(!number_arg(line,&field,&end) || *end || field>(mode==CI_SEARCH_FIELD?14u:13u)) { profile_fields(s,mode==CI_SEARCH_FIELD);return 1; }
        s->profile_edit_field=(int)field-1;s->classic_input=mode==CI_SEARCH_FIELD?CI_SEARCH_VALUE:CI_PROF_VALUE;
        queue_text(s,s->ui.language==LANGUAGE_JA?"内容 > ":"Value > ");return 1;
    }
    if(mode==CI_PROF_VALUE || mode==CI_SEARCH_VALUE) {
        if(mode==CI_SEARCH_VALUE)s->profile_walk=1;
        if(mode==CI_PROF_VALUE && s->profile_edit_target) { snprintf(text,sizeof(text),"MEMBERPROFILE %u %s %s",s->profile_edit_target,classic_fields[s->profile_edit_field],line);classic_run(s,text);return 1; }
        snprintf(text,sizeof(text),"%s %s %s",mode==CI_SEARCH_VALUE?"PROFILEFIND":"PROFILESET",classic_fields[s->profile_edit_field],line);classic_run(s,text);return 1;
    }
    if(mode==CI_BATCH_CLEAR) { if(!strcmp(line,"Y") || !strcmp(line,"y"))classic_run(s,"BATCH CLEAR");else prompt(s);return 1; }
    if(mode==CI_PROTOCOL || mode==CI_SEND_PROTOCOL) {
        const char *names[]={"ZMODEM","XMODEM","YMODEM","YMODEM-G","MLINK","TEXT","XMODEM-SUM","XMODEM-1K"};
        char protocol[32];snprintf(protocol,sizeof(protocol),"%s",line);for(char *p=protocol;*p;p++)if(*p>='a' && *p<='z')*p-=32;
        if(*line) { unsigned selected=8;for(unsigned i=0;i<8;i++)if(!strcmp(protocol,names[i]) || (line[0]=='1'+(int)i && !line[1]))selected=i;
            if(selected==8 || (s->upload_batch && (selected==1 || selected>=5))) { classic_ask(s,mode,MSG_TRANSFER_PROTOCOL);return 1; }s->transfer_protocol=selected; }
        if(s->upload_batch && (s->transfer_protocol==1 || s->transfer_protocol>=5)) { classic_ask(s,mode,MSG_TRANSFER_PROTOCOL);return 1; }
        if(mode==CI_SEND_PROTOCOL) { classic_run(s,"BATCH SEND");return 1; }
        classic_ask(s,CI_UPLOAD,MSG_CLASSIC_UPLOAD);return 1;
    }
    if(mode==CI_FILENAME) {
        if(!*line || strlen(line)>100 || strpbrk(line,"/\\:|") || !safe_text(line) || !strcmp(line,".") || !strcmp(line,"..")) { classic_ask(s,CI_FILENAME,MSG_TRANSFER_FILENAME);return 1; }
        strcpy(s->upload_filename,line);classic_ask(s,CI_FILESIZE,MSG_TRANSFER_SIZE);return 1;
    }
    if(mode==CI_FILESIZE) {
        uint32_t bytes;const char *tail;if((!strcmp(line,"0")?(bytes=0,tail=line+1,0):!number_arg(line,&bytes,&tail)) || *tail || bytes>ln_limit("FILE",1073741824u)) { classic_ask(s,CI_FILESIZE,MSG_TRANSFER_SIZE);return 1; }
        snprintf(text,sizeof(text),"UPLOAD %s|%s|%u|%s",*s->vault_path?s->vault_path:"-",s->upload_filename,bytes,s->upload_description);classic_run(s,text);return 1;
    }
    if(mode==CI_BATCH) {
        if(!strcmp(line,"1"))classic_ask(s,CI_BATCH_ADD,MSG_TRANSFER_ID);
        else if(!strcmp(line,"2"))classic_ask(s,CI_BATCH_REMOVE,MSG_TRANSFER_ID);
        else if(!strcmp(line,"3"))classic_ask(s,CI_BATCH_MATCH,MSG_TRANSFER_MATCH);
        else if(!strcmp(line,"4") || !strcmp(line,"L"))classic_run(s,"BATCH LIST");
        else if(!strcmp(line,"5") || !strcmp(line,"C")) { s->classic_input=CI_BATCH_CLEAR;queue_text(s,"CLEAR? Y/N > "); }
        else if(!strcmp(line,"6") || !strcmp(line,"D")) { s->upload_batch=1;classic_ask(s,CI_SEND_PROTOCOL,MSG_TRANSFER_PROTOCOL); }
        else if(!strcmp(line,"M")) { s->classic_input=CI_BATCH_COPY;queue_text(s,"Destination board (Enter:current) > "); }
        else if(!strcmp(line,"0")) { s->batch_screen=0;prompt(s); }
        else classic_ask(s,CI_BATCH,MSG_TRANSFER_BATCH);
        return 1;
    }
    if(mode==CI_BATCH_ADD || mode==CI_BATCH_REMOVE || mode==CI_BATCH_MATCH) {
        snprintf(text,sizeof(text),"BATCH %s %s",mode==CI_BATCH_ADD?"ADD":mode==CI_BATCH_REMOVE?"REMOVE":"MATCH",line);classic_run(s,text);return 1;
    }
    if(mode==CI_EXIT) {
        if(!strcmp(line,"Y") || !strcmp(line,"y"))classic_run(s,"QUIT");
        else { emit(s,"CANCELLED\r\n");prompt(s); }
        return 1;
    }
    if(mode==CI_HEADER) {
        if(!strcmp(line,"1"))classic_ask(s,CI_OWNER,MSG_CLASSIC_OWNER);
        else if(!strcmp(line,"2"))classic_ask(s,CI_TITLE,MSG_CLASSIC_TITLE);
        else if(strcmp(line,"0"))classic_ask(s,CI_HEADER,MSG_CLASSIC_HEADER);
        else prompt(s);
        return 1;
    }
    if(mode==CI_WITHDRAW) {
        if(!strcmp(line,"Y") || !strcmp(line,"y"))snprintf(text,sizeof(text),"WITHDRAW %u CONFIRM",s->classic_id);
        else { emit(s,"CANCELLED\r\n");prompt(s);return 1; }
    } else if(mode==CI_DATE) {
        if(!*line) { if(s->vault_mode)snprintf(text,sizeof(text),"CHECK FILES %s",*s->vault_path?s->vault_path:"-");else snprintf(text,sizeof(text),"CHECK %s",s->mail_mode?"MAIL":"ARTICLES HERE"); }
        else if(s->vault_mode)snprintf(text,sizeof(text),"FILENEWS %s|%s",*s->vault_path?s->vault_path:"-",line);
        else snprintf(text,sizeof(text),"%s %s",s->mail_mode?"MAIL NEWS":s->vault_mode?"FILENEWS":"NEWS HERE",line);
    } else if(mode==CI_FILEINFO)snprintf(text,sizeof(text),"FILEBOARDINFO %s",*line?line:"-");
    else if(mode==CI_DOWNLOAD)snprintf(text,sizeof(text),"DOWNLOAD %s",line);
    else if(mode==CI_UPLOAD) {
        if(strlen(line)>240 || !safe_text(line) || strchr(line,'|')) { classic_ask(s,CI_UPLOAD,MSG_CLASSIC_UPLOAD);return 1; }
        strcpy(s->upload_description,line);
        if(s->transfer_protocol==1 || s->transfer_protocol>=5) { classic_ask(s,CI_FILENAME,MSG_TRANSFER_FILENAME);return 1; }
        snprintf(text,sizeof(text),"%s %s|%s",s->upload_batch?"UPLOADBATCH":"UPLOAD",*s->vault_path?s->vault_path:"-",line);
    }
    else if(mode==CI_AUTHOR)snprintf(text,sizeof(text),"FIND HERE %s *",line);
    else if(mode==CI_HANDLE)snprintf(text,sizeof(text),"HANDLE %s",line);
    else if(mode==CI_PROFILE)snprintf(text,sizeof(text),"PROFILE %s",line);
    else if(mode==CI_PROFILE_SEARCH)snprintf(text,sizeof(text),"PROFILES %s",line);
    else if(mode==CI_MEMO)snprintf(text,sizeof(text),"MEMO%s%s",*line?" ":"",line);
    else if(mode==CI_TITLE)snprintf(text,sizeof(text),"RETITLE %u %s",s->classic_id,line);
    else if(mode==CI_OWNER)snprintf(text,sizeof(text),"AUTHOR %u %s CONFIRM",s->classic_id,line);
    else if(mode==CI_EDITOR || mode==CI_MENUMODE) {
        char value[32];if(strlen(line)>=sizeof(value)) { emit(s,"ERR command\r\n");prompt(s);return 1; }
        strcpy(value,line);for(char *p=value;*p;p++)if(*p>='a' && *p<='z')*p-=32;
        snprintf(text,sizeof(text),"%s %s",mode==CI_EDITOR?"EDITOR":"MENUMODE",value);
    } else if(mode==CI_ROWS) {
        uint32_t rows;const char *tail;
        if(!number_arg(line,&rows,&tail) || *tail || (rows && (rows<5 || rows>97))) { emit(s,"ERR rows 0 or 5..97\r\n");prompt(s);return 1; }
        if(rows) { session_dimensions(s,s->columns,rows+3);s->pager_enabled=1; }
        else s->pager_enabled=0;
        emit(s,rows?"PAGER ON\r\n":"PAGER OFF\r\n");prompt(s);return 1;
    } else { emit(s,"ERR command\r\n");prompt(s);return 1; }
    classic_run(s,text);return 1;
}
static int classic_commands(Session *s,const char *line) {
    if(!s->ui.enabled || s->ui.input)return 0;
    if(classic_input(s,line))return 1;
    if(!strcmp(line,"AUDIT REFRESH") && !s->mail_input && !s->mail_delete) {
        char mapped[256];
        if(s->ui.view==UI_AUDIT)audit_command(&s->ui,0,mapped,sizeof(mapped));
        else strcpy(mapped,"AUDIT");
        command_core(s,mapped);prompt(s);return 1;
    }
    if(!s->mail_mode && !s->vault_mode && !s->mail_input && !s->mail_delete) {
        if(!strcmp(line,"REGISTER") || !strcmp(line,"LOGIN")) {
            if(s->user)emit(s,"ERR logout first\r\n");
            else if(!s->member_mode)emit(s,"ERR members disabled\r\n");
            else if(*line=='R' && members_auto_ids()) { classic_run(s,"REGISTER auto");return 1; }
            else s->ui.input=*line=='R'?UI_REGISTER:UI_LOGIN;
            prompt(s);return 1;
        }
        if(!strcmp(line,"POST") || !strcmp(line,"REPLY")) {
            s->classic_area=CM_BOARD;
            if(*line=='R' && !s->current)emit(s,"ERR no current article\r\n");
            else if(s->member_mode && !s->user) { BoardRequest q={0};q.op='e';q.action='?';q.number=*line=='R'?s->current:0;submit(s,&q); }
            else s->ui.input=*line=='R'?UI_REPLY:UI_POST;
            prompt(s);return 1;
        }
        if(!strcmp(line,"SEARCH")) { s->ui.input=UI_SEARCH_INPUT;prompt(s);return 1; }
        if(!strcmp(line,"SEEN NEXT")) { s->ui.next_after_seen=1;classic_run(s,"SEEN");return 1; }
    }
    /* These commands explicitly switch screens, independent of the last report. */
    if(!strcmp(line,"MENU") && !s->mail_input && !s->mail_delete) {
        s->mail_mode=s->vault_mode=0;s->file_selected=0;s->batch_screen=0;s->classic_area=CM_MAIN;return 0;
    }
    if(!strcmp(line,"BOARDS") || !strncmp(line,"BOARD ",6) || !strcmp(line,"NOTES") ||
       !strncmp(line,"NOTES ",6) || !strncmp(line,"READ ",5) || !strncmp(line,"SCAN ",5) ||
       !strncmp(line,"POST ",5) || !strncmp(line,"REPLY ",6))s->classic_area=CM_BOARD;
    if((!strcmp(line,"O") || !strcmp(line,"Q")) && !s->mail_input && !s->mail_delete) { classic_ask(s,CI_EXIT,MSG_EXIT_CONFIRM);return 1; }
    if(s->mail_mode) {
        if(s->mail_reading && !s->mail_input && !s->mail_delete && (!*line || !strcmp(line,"P") || !strcmp(line,"L") || !strcmp(line,"F") || *line=='+' || *line=='-')) {
            uint32_t distance=1;const char *tail;
            if((*line=='+' || *line=='-') && line[1] && (!number_arg(line+1,&distance,&tail) || *tail || !distance)) { emit(s,"ERR relative distance\r\n");prompt(s);return 1; }
            classic_read(s,*line=='P' || *line=='-'?1:*line=='+'?0:s->classic_direction,distance,*line=='L');
            if(*line=='F' && s->pending)s->auto_read=1;
            return 1;
        }
        if(s->mail_input || s->mail_delete)return 0;
        if(!strcmp(line,"N")) { classic_ask(s,CI_DATE,MSG_CLASSIC_DATE);return 1; }
        if(!strcmp(line,"T")) { classic_run(s,"MAIL SCAN DESC");return 1; }
        if(!strcmp(line,"0") || !strcmp(line,"EXIT"))s->classic_area=CM_MAIN;
        return 0;
    }
    if(s->vault_mode) {
        if(!strcmp(line,"N")) { classic_ask(s,CI_DATE,MSG_CLASSIC_DATE);return 1; }
        if(!strcmp(line,"B")) { s->batch_screen=1;s->file_selected=0;classic_ask(s,CI_BATCH,MSG_TRANSFER_BATCH);return 1; }
        if(!strcmp(line,"I")) { char info[80];snprintf(info,sizeof(info),"FILEBOARDINFO %s",*s->vault_path?s->vault_path:"-");classic_run(s,info);return 1; }
        if(!strcmp(line,"D")) { classic_ask(s,CI_DOWNLOAD,MSG_CLASSIC_DOWNLOAD);return 1; }
        if(!strcmp(line,"U") || !strcmp(line,"UB")) { s->upload_batch=!strcmp(line,"UB");classic_ask(s,CI_PROTOCOL,MSG_TRANSFER_PROTOCOL);return 1; }
        if(!strcmp(line,"0") || !strcmp(line,"EXIT"))s->classic_area=CM_MAIN;
        return 0;
    }
    if(!strcmp(line,"?")) { show_menu(s,1);prompt(s);return 1; }
    if(s->classic_area==CM_SETTINGS) {
        if(!strcmp(line,"H")) { classic_ask(s,CI_HANDLE,MSG_CLASSIC_HANDLE);return 1; }
        if(!strcmp(line,"1")) { classic_ask(s,CI_EDITOR,MSG_CLASSIC_EDITOR);return 1; }
        if(!strcmp(line,"3")) { classic_ask(s,CI_MENUMODE,MSG_CLASSIC_MENUMODE);return 1; }
        if(!strcmp(line,"4")) { classic_ask(s,CI_ROWS,MSG_CLASSIC_ROWS);return 1; }
        if(!strcmp(line,"2")) { queue_text(s,ui_text(&s->ui,MSG_CLASSIC_NO_ESC));prompt(s);return 1; }
    }
    if(!strcmp(line,"0")) {
        if(s->classic_area==CM_BOARD && s->ui.view==UI_ARTICLE)classic_run(s,"NOTES");
        else classic_run(s,"MENU");
        return 1;
    }
    if(!strcmp(line,"H")) { show_help(s,s->classic_area==CM_BOARD?"READ":s->classic_area==CM_SETTINGS?"SETTINGS":s->classic_area==CM_PROFILE?"PROFILE":s->classic_area==CM_ADMIN?"ADMIN":"");prompt(s);return 1; }
    if(!strcmp(line,"O") || !strcmp(line,"Q")) { classic_run(s,"QUIT");return 1; }
    if(!strcmp(line,"$")) {
        if(!s->ui.admin && !(s->classic_area==CM_BOARD && s->ui.sigop)) { emit(s,"ERR admin required\r\n");prompt(s);return 1; }
        s->classic_area=CM_ADMIN;show_menu(s,1);prompt(s);return 1;
    }
    if(s->classic_area==CM_ADMIN && strlen(line)==1 && *line>='1' && *line<='5') {
        if(s->ui.admin)show_help(s,(const char *[]){"ADMINMEMBERS","ADMINBOARDS","ADMINFILES","ADMINCONNECTIONS","ADMINSTORAGE"}[*line-'1']);
        else if(s->ui.sigop)show_help(s,"SIGOP");else emit(s,"ERR admin required\r\n");
        prompt(s);return 1;
    }
    if(s->classic_area==CM_BOARD && s->ui.view==UI_ARTICLE) {
        if(!*line || !strcmp(line,"P") || !strcmp(line,"L") || !strcmp(line,"F") || *line=='+' || *line=='-') {
            uint32_t distance=1;const char *tail;
            if((*line=='+' || *line=='-') && line[1] && (!number_arg(line+1,&distance,&tail) || *tail || !distance)) { emit(s,"ERR relative distance\r\n");prompt(s);return 1; }
            int direction=*line=='P' || *line=='-'?1:*line=='+'?0:s->classic_direction;
            classic_read(s,direction,distance,*line=='L');if(*line=='F' && s->pending)s->auto_read=1;return 1;
        }
        if(!strcmp(line,"S")) { BoardRequest q={0};q.op='Z';q.action=17;q.number=s->current;submit(s,&q);return 1; }
        if(!strcmp(line,"D")) { classic_run(s,"PARENT");return 1; }
        if(!strcmp(line,"A")) { classic_run(s,"CHILD");return 1; }
        if(!strcmp(line,"G")) { classic_run(s,"GET");return 1; }
        if(!strcmp(line,"K")) { s->classic_id=s->current;classic_ask(s,CI_WITHDRAW,MSG_CLASSIC_DELETE);return 1; }
        if(!strcmp(line,"E")) {
            if(!s->ui.admin && !s->ui.sigop) { emit(s,"ERR admin required\r\n");prompt(s);return 1; }
            s->classic_id=s->current;classic_ask(s,CI_HEADER,MSG_CLASSIC_HEADER);return 1;
        }
    }
    if(strlen(line)!=1 || *line=='+' || *line=='>' || *line=='<' || (*line>='0' && *line<='9'))return 0;
    const char *action=NULL;char key=*line;
    if(s->classic_area==CM_MAIN) {
        switch(key) {
        case 'A':action="REGISTER";break;
        case 'B': action="BOARDS";break;case 'C':action="CHAT";break;case 'D':action="CALL";break;
        case 'E':action="BUFFER";break;case 'F':action="VAULT";break;case 'G':action="GATHER";break;
        case 'I':s->classic_area=CM_SETTINGS;show_menu(s,1);prompt(s);return 1;
        case 'L':action="USERTABLE";break;case 'M':action="MAIL";break;
        case 'P':s->classic_area=CM_PROFILE;show_menu(s,1);prompt(s);return 1;
        case 'T':action="TALK";break;case 'U':action="PROFILES";break;case 'W':action="ONLINE";break;
        case '\\':classic_ask(s,CI_MEMO,MSG_CLASSIC_MEMO);return 1;
        case '#':action="ACCOUNT";break;
        case 'S':queue_text(s,*site_text(0)?site_text(0):ui_text(&s->ui,MSG_SITE_EMPTY));prompt(s);return 1;
        case 'V':queue_text(s,"Liner notes 0.73 (stage73)\r\n");prompt(s);return 1;
        case 'X':queue_text(s,ui_text(&s->ui,MSG_CLASSIC_NO_GATEWAY));prompt(s);return 1;
        }
    } else if(s->classic_area==CM_BOARD) {
        switch(key) {
        case 'B':action="BOARDS";break;case 'I':action="INFO";break;case 'L':action="NOTES";break;
        case 'T':action="SCAN DESC HERE * *";break;
        case 'N':classic_ask(s,CI_DATE,MSG_CLASSIC_DATE);return 1;
        case 'F':classic_ask(s,CI_AUTHOR,MSG_CLASSIC_AUTHOR);return 1;
        case 'E':action="POST";break;
        case 'R':if(!s->current) { emit(s,"ERR no current article\r\n");prompt(s);return 1; }s->ui.input=UI_REPLY;prompt(s);return 1;
        case 'K':s->classic_id=s->current;classic_ask(s,CI_WITHDRAW,MSG_CLASSIC_DELETE);return 1;
        }
    } else if(s->classic_area==CM_SETTINGS) {
        switch(key) {
        case 'D': {
            char settings[512];snprintf(settings,sizeof(settings),"SETTINGS\r\nLANG %s / EDITOR %s / MENUMODE %s\r\nPAGER %s / SIZE %u x %u / TIMEZONE %s\r\nPROTOCOL %s / LINE NUMBERS %s\r\n",language_code(s->ui.language),s->editor_line?"LINE":"BETA",s->menu_mode==0?"BEGINNER":s->menu_mode==2?"EXPERT":"STANDARD",s->pager_enabled?"ON":"OFF",s->columns,s->rows,date_zone(),(const char *[]){"ZMODEM","XMODEM","YMODEM","YMODEM-G","MLINK","TEXT","XMODEM-SUM","XMODEM-1K"}[s->transfer_protocol],s->editor_numbers?"ON":"OFF");
            queue_text(s,settings);show_menu(s,1);prompt(s);return 1;
        }
        case 'P':action="PASSWD";break;
        case 'H':classic_ask(s,CI_HANDLE,MSG_CLASSIC_HANDLE);return 1;
        }
    } else if(s->classic_area==CM_PROFILE) {
        switch(key) {
        case 'L':action="PROFILES";break;case 'P':classic_ask(s,CI_PROFILE,MSG_CLASSIC_PROFILE);return 1;
        case 'S':profile_fields(s,1);return 1;
        case 'W':action="BIOEDIT";break;case 'E':s->profile_edit_target=0;if(s->ui.admin) { s->classic_input=CI_PROF_TARGET;queue_text(s,"Member ID (Enter:self) > "); }else profile_fields(s,0);return 1;
        }
    }
    if(action)classic_run(s,action);
    else { emit(s,"ERR command\r\n");prompt(s); }
    return 1;
}
