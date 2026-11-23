#include "mmm_input.h"
#include <string.h>

static const struct {
    const char *name, *usage;
    MmmCommand command;
    int auth;
} commands[] = {
    {"login","login [id]",MMM_LOGIN,0},
    {"register","register id",MMM_REGISTER,0},
    {"off","off [-n]",MMM_OFF,0},
    {"ui","ui [rtbbs|mmm|reset]",MMM_UI,0},
    {"logout","logout",MMM_LOGOUT,1},
    {"notes","notes",MMM_NOTES,1},
    {"go","go board",MMM_GO,1},
    {"open","open [-i] [-bNUMBER] [board]",MMM_OPEN,1},
    {"post","post [board]",MMM_POST,1},
    {"mail","mail",MMM_MAIL,1},
    {"filer","filer [board]",MMM_FILER,1},
    {"chat","chat",MMM_CHAT,1},
    {"term","term",MMM_TERM,1},
    {"profile","profile [id]",MMM_PROFILE,1},
    {"games","games",MMM_GAMES,1},
    {"new","new [-n]",MMM_NEW,1},
    {"setsequencer","setsequencer [YYYY-MM-DD|reset]",MMM_SEQUENCER,1}
};

static int space(char ch) { return ch==' ' || ch=='\t'; }
static char fold(char ch) { return ch>='A' && ch<='Z'?(char)(ch+'a'-'A'):ch; }

static const char *number(const char *p, uint32_t *out) {
    uint32_t n=0;
    if(*p<'0' || *p>'9')return NULL;
    for(;*p>='0' && *p<='9';p++) {
        unsigned digit=(unsigned)(*p-'0');
        if(n>(UINT32_MAX-digit)/10)return NULL;
        n=n*10+digit;
    }
    if(!n)return NULL;
    *out=n;
    return p;
}

int mmm_number(const char *text, uint32_t *out) {
    const char *end=number(text,out);
    return end && !*end;
}

MmmInput mmm_parse(char *line, int authenticated) {
    while(space(*line))line++;
    size_t n=strlen(line);
    while(n && space(line[n-1]))line[--n]=0;
    if(!n)return (MmmInput){.command=MMM_MENU};
    if(n==1) {
        if(*line=='/')return (MmmInput){.command=MMM_HOME};
        if(*line=='.')return (MmmInput){.command=MMM_UP};
        if(*line=='?')return (MmmInput){.command=MMM_HELP};
    }
    uint32_t selection;
    if(mmm_number(line,&selection))return (MmmInput){.command=MMM_NUMBER,.number=selection};
    char *argument=line;
    while(*argument && !space(*argument)) {
        *argument=fold(*argument);
        argument++;
    }
    size_t length=(size_t)(argument-line);
    if(*argument)*argument++=0;
    while(space(*argument))argument++;
    MmmCommand match=MMM_UNKNOWN;
    for(size_t i=0;i<sizeof(commands)/sizeof(commands[0]);i++) {
        if(commands[i].auth && !authenticated)continue;
        if(strncmp(line,commands[i].name,length))continue;
        if(!commands[i].name[length]) {
            match=commands[i].command;
            break;
        }
        match=match==MMM_UNKNOWN?commands[i].command:MMM_AMBIGUOUS;
    }
    return (MmmInput){.command=match,.argument=argument,.usage=!strcmp(argument,"-?")};
}

void mmm_command_help(MmmCommand command, const char **name, const char **usage) {
    *name=*usage="";
    for(size_t i=0;i<sizeof(commands)/sizeof(commands[0]);i++) {
        if(command!=commands[i].command)continue;
        *name=commands[i].name;
        *usage=commands[i].usage;
        return;
    }
}

static const char *endpoint(const char *p, uint32_t lines, uint32_t *value) {
    if(*p=='$') { *value=lines;return p+1; }
    return number(p,value);
}

int mmm_range(const char *text,uint32_t lines,uint32_t *first,uint32_t *last,
              int *complement,const char **tail) {
    const char *p=text;
    while(space(*p))p++;
    *complement=*p=='!';
    if(*complement) { p++;while(space(*p))p++; }
    *first=1;*last=lines;
    const char *end=endpoint(p,lines,first);
    if(end) {
        p=end;
        while(space(*p))p++;
        *last=*first;
    }
    if(*p==',') {
        p++;
        while(space(*p))p++;
        *last=lines;
        end=endpoint(p,lines,last);
        if(end)p=end;
    }
    while(space(*p))p++;
    *tail=p;
    return lines && *first && *first<=*last && *last<=lines;
}
