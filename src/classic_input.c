#include "classic_input.h"
#include <string.h>
static int word_is(const char *p,size_t n,const char *word) {
    if(strlen(word)!=n)return 0;
    for(size_t i=0;i<n;i++) { unsigned c=(unsigned char)p[i];if(c>='a' && c<='z')c-=32;if(c!=(unsigned char)word[i])return 0; }return 1;
}
int classic_input_uppercase(const ClassicInput *c,const char *line,size_t size) {
    if(c->memo)return 0;
    if(c->selection)return 1;
    if(c->application || c->classic)return 0;
    if(c->secret || c->input || c->mail_input || c->paging || c->binary || c->transfer)return 0;
    if(c->editing || c->chatting || c->gather || c->talk) {
        if(!size || line[0]!='/')return !c->chatting && c->editing && c->line_editor && !c->editor_input && !memchr(line,' ',size);
        if(size>1 && line[1]=='/')return 0;
        const char *space=memchr(line,' ',size);
        if(!space)return 1;
        size_t first=(size_t)(space-line);
        return (word_is(line,first,"/EDITOR") || word_is(line,first,"/LANG") || word_is(line,first,"/NOTIFY")) && !memchr(space+1,' ',size-first-1);
    }
    const char *space=memchr(line,' ',size);if(!space)return 1;
    size_t n=(size_t)(space-line),offset=n+1;
    const char *words[]={"MENUMODE","BOARDGUEST","LANG","HELP","PAGER","PREFS","PROTOCOL","EDITOR","MAIL","BATCH","CHAT","TALK","NOTES","USERLOG","USERTABLE","USERSTATS","FILESTATS","FILEHISTORY","SHUTDOWN","AUDIT","FIND","SCAN","NEWS","NOTIFY","DRAFT","BUFFER","CHECK"};
    for(unsigned i=0;i<sizeof(words)/sizeof(words[0]);i++)if(word_is(line,n,words[i]))return !memchr(line+offset,' ',size-offset);
    return 0;
}
static char *upper_word(char *p) { for(;*p && *p!=' ';p++)if(*p>='a' && *p<='z')*p-=32;return *p?p+1:p; }
void classic_input_normalize(const ClassicInput *c,char *line,size_t n) {
    /* Hidden input remains case-sensitive; only the exact cancellation token folds. */
    if(word_is(line,n,"/CANCEL")) { memcpy(line,"/CANCEL",7);return; }
    if(c->memo)return;
    if(c->selection) { for(char *p=line;*p;p++)if(*p>='a' && *p<='z')*p-=32;return; }
    if(c->application || c->classic)return;
    if(c->secret)return;
    if(!strncmp(line,"//",2))return;
    for(size_t i=0;i<n;i++)if(classic_input_uppercase(c,line,i) && line[i]>='a' && line[i]<='z')line[i]=(char)(line[i]-32);
    if(c->editing || c->chatting || c->gather || c->talk || c->input || c->mail_input)return;
    if(!strncmp(line,"MAIL SCAN ",10))upper_word(line+10);
    if(!strncmp(line,"LINECONF ",9)) { char *verb=line+9;while(*verb==' ')verb++;upper_word(verb); }
    if(!strncmp(line,"FIND ",5) || !strncmp(line,"SCAN ",5)) { char *scope=upper_word(line+5);if(!strncmp(line+5,"ASC ",4) || !strncmp(line+5,"DESC ",5))upper_word(scope); }
    if(!strncmp(line,"FINDFROM ",9)) { char *p=strchr(line+9,' ');if(p)upper_word(p+1); }
    if(!strncmp(line,"MEMBERPROFILE ",14) || !strncmp(line,"APPLICATIONSET ",15)) { char *p=strchr(line+(*line=='M'?14:15),' ');if(p)upper_word(p+1); }
    if(!strncmp(line,"FILEBOARDOPTIONS ",17)) { char *p=strchr(line+17,'|');if(p)upper_word(p+1); }
    if(!strncmp(line,"FILEBOARDPASS ",14)) { char *p=strchr(line+14,' ');if(p)upper_word(p+1); }
    if(!strncmp(line,"MAIL DRAFT ",11) && strlen(line)>=18 && word_is(line+11,6,"DELETE"))memcpy(line+11,"DELETE",6);
    if(!strncmp(line,"MAIL NOTIFY ",12) || !strncmp(line,"MAIL DROP ",10) || (c->mail_mode && !strncmp(line,"DROP ",5))) {
        char *last=strrchr(line,' ');if(last && (word_is(last+1,strlen(last+1),"ON") || word_is(last+1,strlen(last+1),"OFF") || word_is(last+1,strlen(last+1),"CONFIRM")))for(char *p=last+1;*p;p++)if(*p>='a' && *p<='z')*p-=32;
    }
    if(!strncmp(line,"BOARDPASS ",10)) { char *p=strchr(line+10,' ');if(p && strlen(p)>=5 && word_is(p+1,4,"NONE"))memcpy(p+1,"NONE",4); }
    if(!strncmp(line,"PROFILEVIS ",11)) { char *last=strrchr(line,' ');if(last)for(char *p=last+1;*p;p++)if(*p>='a' && *p<='z')*p-=32; }
    /* Confirmation tokens are syntax only on operations that accept them. */
    if(!strncmp(line,"BOARDGUEST ",11)) { char *p=strchr(line+11,' ');if(p)upper_word(p+1); }
    if(!strncmp(line,"BOARDDELETEALL ",15)) { char *p=strchr(line+15,' ');if(p)upper_word(p+1); }
    const char *confirm[]={"AUTHOR ","MOVEBRANCH ","BOARDDELETEALL ","FILEDELETE ","FILEHIDE ","FILERESTORE ","WITHDRAW ","DISCONNECT ","BOARDDELETE ","MOVE ","BOARDPASS ","GROUPMOVE ","BOARDGROUP ","FILEBOARDPASS "};
    for(unsigned i=0;i<sizeof(confirm)/sizeof(confirm[0]);i++)if(!strncmp(line,confirm[i],strlen(confirm[i]))) {
        char *last=strrchr(line,' ');if(last && word_is(last+1,strlen(last+1),"CONFIRM"))memcpy(last+1,"CONFIRM",7);
    }
}
