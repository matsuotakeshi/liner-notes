#include "net_cock_input.h"
#include <string.h>

typedef struct { const char *name; NetCommand command; } CommandName;
/* Exact names win; historic shortcuts win over ordered prefix fallback. */
static const CommandName commands[]={
    {"OFF",NC_OFF},{"BREAD",NC_BREAD},{"BWRITE",NC_BWRITE},{"BATCH",NC_BATCH},
    {"BSET",NC_BSET},{"RALL",NC_RALL},{"RNALL",NC_RNALL},{"IDLIST",NC_IDLIST},
    {"UREAD",NC_UREAD},{"UWRITE",NC_UWRITE},{"USTAT",NC_USTAT},{"LOG",NC_LOG},
    {"LLIST",NC_LLIST},{"MODE",NC_MODE},{"MREAD",NC_MREAD},{"MWRITE",NC_MWRITE},
    {"MCHECK",NC_MCHECK},{"MBSET",NC_MBSET},{"PASS",NC_PASS},{"PMOVE",NC_PMOVE},
    {"CHAT",NC_CHAT},{"COFF",NC_COFF},{"CON",NC_CON},{"CLS",NC_CLS},
    {"VERSION",NC_VERSION},{"ACCESS",NC_ACCESS},{"JOIN",NC_JOIN},{"NEWMEM",NC_NEWMEM},
    {"EXCOM",NC_EXCOM},{"TSET",NC_TSET},{"CSET",NC_CSET},{"PDS",NC_PDS},
    {"GAMES",NC_GAMES},{"UI",NC_UI},{"TOPIC",NC_TOPIC},{"TALK",NC_TALK},
    {"DRAFT",NC_DRAFT},{"FIND",NC_FIND},{"ADMIN",NC_ADMIN},{"HELP",NC_HELP},
    {"BMAKE",NC_BMAKE},{"BCHANGE",NC_BCHANGE},{"BKILL",NC_BKILL},{"BTITLE",NC_BTITLE},
    {"BUSER",NC_BUSER},{"MAKEID",NC_MAKEID},{"GULV",NC_GULV},{"ULEVEL",NC_ULEVEL},
    {"UTIME",NC_UTIME},{"UPASS",NC_UPASS},{"UDLIST",NC_UDLIST},{"UDEDIT",NC_UDEDIT},
    {"SIGNUP",NC_SIGNUP},{"AOFF",NC_AOFF},{"AON",NC_AON},{"SDTIME",NC_SDTIME},
    {"REPORT",NC_REPORT},{"MESEDIT",NC_MESEDIT}
};
static const CommandName shortcuts[]={
    {"B",NC_BREAD},{"BW",NC_BWRITE},{"BA",NC_BATCH},{"BS",NC_BSET},
    {"RA",NC_RALL},{"RN",NC_RNALL},{"P",NC_PMOVE},{"E",NC_EXCOM},{"#",NC_EXCOM},
    {"L",NC_LOG},{"LL",NC_LLIST},{"*",NC_LLIST},{"M",NC_MODE},{"MW",NC_MWRITE},
    {"MC",NC_MCHECK},{"MB",NC_MBSET},{"U",NC_UREAD},{"UW",NC_UWRITE},{"I",NC_IDLIST},
    {"US",NC_USTAT},{"PA",NC_PASS},{"CL",NC_CLS},{"C",NC_CHAT},{"CS",NC_CSET},
    {"T",NC_TSET},{"A",NC_ACCESS},{"V",NC_VERSION},{"O",NC_OFF},{"?",NC_HELP}
};

static unsigned char upper(unsigned char c) {
    return c>='a' && c<='z'?(unsigned char)(c-'a'+'A'):c;
}
static int prefix(const char *input,const char *name) {
    while(*input && *name && upper((unsigned char)*input)==upper((unsigned char)*name)) { input++;name++; }
    return !*input;
}
int net_cock_equal(const char *a,const char *b) {
    return strlen(a)==strlen(b) && prefix(a,b);
}
NetInput net_cock_split(char *line) {
    line+=strspn(line," \t");
    char *end=line+strcspn(line," \t");
    if(*end)*end++=0;
    return (NetInput){line,end+strspn(end," \t")};
}
NetCommand net_cock_command(const char *word) {
    if(!*word)return NC_UNKNOWN;
    for(unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);i++)
        if(net_cock_equal(word,commands[i].name))return commands[i].command;
    for(unsigned i=0;i<sizeof(shortcuts)/sizeof(shortcuts[0]);i++)
        if(net_cock_equal(word,shortcuts[i].name))return shortcuts[i].command;
    /* Management operations require a full command name. */
    for(unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);i++)
        if(commands[i].command<NC_BMAKE && prefix(word,commands[i].name))return commands[i].command;
    return NC_UNKNOWN;
}

NetCommand net_cock_menu_command(NetMenu menu,unsigned number,int authenticated,int administrator) {
    static const NetCommand guest[]={NC_UREAD,NC_BREAD,NC_CHAT,NC_JOIN,NC_OFF};
    static const NetCommand member[]={NC_UREAD,NC_BREAD,NC_MREAD,NC_CHAT,NC_JOIN,NC_OFF};
    static const NetCommand admin[]={NC_UREAD,NC_BREAD,NC_MREAD,NC_MODE,NC_ADMIN,NC_CHAT,NC_JOIN,NC_REPORT,NC_OFF};
    static const NetCommand user[]={NC_UREAD,NC_UWRITE,NC_IDLIST,NC_USTAT,NC_MODE,NC_PASS};
    static const NetCommand board[]={NC_BREAD,NC_BWRITE,NC_BATCH,NC_BSET,NC_RALL,NC_RNALL,NC_PMOVE,NC_EXCOM};
    static const NetCommand mail[]={NC_MREAD,NC_MWRITE,NC_MCHECK,NC_MBSET};
    static const NetCommand chat[]={NC_CHAT,NC_LLIST,NC_TSET,NC_CSET,NC_ACCESS};
    static const NetCommand join[]={NC_JOIN,NC_NEWMEM};
    if(!number)return NC_UNKNOWN;
#define SELECT(table) return number<=sizeof(table)/sizeof((table)[0])?(table)[number-1]:NC_UNKNOWN
    switch(menu) {
    case NC_MAIN_MENU:
        if(administrator) { SELECT(admin); }
        if(authenticated) { SELECT(member); }
        SELECT(guest);
    case NC_USER_MENU: SELECT(user);
    case NC_BOARD_MENU: SELECT(board);
    case NC_MAIL_MENU: SELECT(mail);
    case NC_CHAT_MENU: SELECT(chat);
    case NC_REGISTER_MENU: SELECT(join);
    default:return NC_UNKNOWN;
    }
#undef SELECT
}
