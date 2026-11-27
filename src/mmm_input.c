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
    {"ui","ui [rtbbs|mmm|mash|ktbbs|big-model|wani-bbs|reset]",MMM_UI,0},
    {"logout","logout",MMM_LOGOUT,1},
    {"notes","notes",MMM_NOTES,1},
    {"go","go board",MMM_GO,1},
    {"open","open [-i|-s|-x|-bNUMBER|-n [-a]] [#board|@label|title]",MMM_OPEN,1},
    {"post","post [board]",MMM_POST,1},
    {"mail","mail",MMM_MAIL,1},
    {"filer","filer [board]",MMM_FILER,1},
    {"chat","chat",MMM_CHAT,1},
    {"term","term",MMM_TERM,1},
    {"profile","profile [id]",MMM_PROFILE,1},
    {"games","games",MMM_GAMES,1},
    {"new","new [-n]",MMM_NEW,1},
    {"setsequencer","setsequencer [YYYY-MM-DD|reset]",MMM_SEQUENCER,1},
    {"scanlist","scanlist [set #BOARD|@LABEL|@PATTERN|* [!TARGET] ... | reset | -d]",MMM_SCANLIST,1},
    {"boardlabel","boardlabel BOARD [LABEL|NONE]",MMM_BOARDLABEL,1},
    {"draft","draft [resume|drop confirm]",MMM_DRAFT,1},
    {"retitle","retitle NOTE TITLE",MMM_RETITLE,1},
    {"withdraw","withdraw NOTE confirm",MMM_WITHDRAW,1},
    {"manage","manage",MMM_MANAGE,1},
    {"pfile","pfile [-t|-e|-d confirm|-b [bytes]]",MMM_PFILE,1},
    {"regsign","regsign [-e|-t|-a|-n|-d confirm]",MMM_SIGNATURE,1},
    {"talk","talk [-c|-n]",MMM_TALK,1}
};

static int space(char ch) { return ch==' ' || ch=='\t'; }
static char fold(char ch) { return ch>='A' && ch<='Z'?(char)(ch+'a'-'A'):ch; }

MashInput mash_parse(const char *line) {
    static const struct { const char *name, *usage; MashCommand command; } commands[]={
        {"bbs","bbs",MASH_BBS}, {"setup","setup",MASH_SETUP},
        {"pprof","pprof [ID]",MASH_PROFILES}, {"whoami","whoami",MASH_MY_PROFILE},
        {"who","who",MASH_WHO}, {"w","who",MASH_WHO},
        {"map","map",MASH_MAP}, {"manual","manual",MASH_MANUAL},
        {"cmdlst","cmdlst",MASH_COMMANDS}, {"others","others",MASH_OTHERS},
        {"readnews","readnews [-n]",MASH_NEWS},
        {"sets","sets [YYYY-MM-DD|reset]",MASH_SEQUENCER}, {"bye","bye",MASH_BYE},
        {"!","! CONNECTION TEXT",MASH_WIRE}, {"$","$ [TEXT]",MASH_SYSOP},
        {"version","version",MASH_VERSION}, {"v","version",MASH_VERSION},
        {"c","chat",MASH_CHAT}
    };
    while(space(*line))line++;
    const char *end=line;
    while(*end && !space(*end))end++;
    const char *arg=end;
    while(space(*arg))arg++;
    for(unsigned i=0;i<sizeof(commands)/sizeof(*commands);i++) {
        const char *name=commands[i].name;
        if((size_t)(end-line)!=strlen(name))continue;
        const char *p=line;
        while(p!=end && fold(*p)==*name) { p++;name++; }
        if(p==end)return (MashInput){commands[i].command,arg,commands[i].usage};
    }
    return (MashInput){0};
}

const char *mash_menu_command(int others,uint32_t n) {
    static const char *const main[]={NULL,"mail","bbs","setup","pprof","filer",
        "others","off","whoami","manual","map",NULL,"readnews -n","new -n","talk -n"};
    static const char *const online[]={NULL,"who","chat","talk","games"};
    if(others)return n<sizeof(online)/sizeof(*online)?online[n]:NULL;
    return n<sizeof(main)/sizeof(*main)?main[n]:NULL;
}

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

static char *split_word(char *text) {
    while(*text && !space(*text))text++;
    if(*text)*text++=0;
    while(space(*text))text++;
    return text;
}

int mmm_open_parse(char *arg,MmmOpen *out) {
    *out=(MmmOpen){.mode=MMM_OPEN_INDEX};
    while(*arg=='-') {
        char *next=split_word(arg);
        if(!arg[1])return 0;
        switch(fold(arg[1])) {
        case 'i': if(arg[2])return 0;break;
        case 'n': if(arg[2] || out->continuous)return 0;out->continuous=1;break;
        case 'a': if(arg[2] || out->all)return 0;out->all=1;break;
        case 's': case 'x':
            if(arg[2] || out->mode!=MMM_OPEN_INDEX)return 0;
            out->mode=fold(arg[1])=='s'?MMM_OPEN_NEW:MMM_OPEN_NEW_OR_INDEX;
            break;
        case 'b':
            if(out->mode!=MMM_OPEN_INDEX || !mmm_number(arg+2,&out->article))return 0;
            out->mode=MMM_OPEN_ARTICLE;
            break;
        default: return 0;
        }
        arg=next;
    }
    out->board=arg;
    return (!out->all || out->continuous) && (!out->continuous || out->mode==MMM_OPEN_INDEX);
}

int mmm_scanlist_parse(char *arg,MmmScanTargets *out) {
    memset(out,0,sizeof(*out));
    while(space(*arg))arg++;
    while(*arg) {
        if(out->count==MMM_SCAN_MAX)return 0;
        char *next=split_word(arg);
        MmmScanMode mode=MMM_SCAN_NEW;
        if(!strcmp(arg,"-i") || !strcmp(arg,"-s") || !strcmp(arg,"-x")) {
            mode=arg[1]=='i'?MMM_SCAN_INDEX:arg[1]=='x'?MMM_SCAN_FALLBACK:MMM_SCAN_NEW;
            if(!*next)return 0;
            arg=next;next=split_word(arg);
        }
        MmmBoardTarget *target=&out->targets[out->count++];
        target->mode=mode;
        if(*arg=='!') { if(mode!=MMM_SCAN_NEW)return 0;target->exclude=1;arg++; }
        if(*arg=='*' && !arg[1])strcpy(target->label,"*");
        else if(*arg=='@') {
            size_t n=strlen(arg+1);
            if(!n || n>MMM_LABEL_MAX)return 0;
            memcpy(target->label,arg+1,n+1);
        } else if(!mmm_number(*arg=='#'?arg+1:arg,&target->board))return 0;
        arg=next;
    }
    return out->count!=0;
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
