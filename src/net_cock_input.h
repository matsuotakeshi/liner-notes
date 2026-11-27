#ifndef LINER_NET_COCK_INPUT_H
#define LINER_NET_COCK_INPUT_H
#include <stdint.h>

typedef enum {
    NC_UNKNOWN, NC_OFF, NC_BREAD, NC_BWRITE, NC_BATCH, NC_BSET, NC_RALL, NC_RNALL,
    NC_IDLIST, NC_UREAD, NC_UWRITE, NC_USTAT, NC_LOG, NC_LLIST, NC_MODE, NC_MREAD,
    NC_MWRITE, NC_MCHECK, NC_MBSET, NC_PASS, NC_PMOVE, NC_CHAT, NC_COFF, NC_CON,
    NC_CLS, NC_VERSION, NC_ACCESS, NC_JOIN, NC_NEWMEM, NC_EXCOM, NC_TSET, NC_CSET,
    NC_PDS, NC_GAMES, NC_UI, NC_TOPIC, NC_TALK, NC_DRAFT, NC_FIND, NC_ADMIN, NC_HELP,
    NC_BMAKE, NC_BCHANGE, NC_BKILL, NC_BTITLE, NC_BUSER, NC_MAKEID, NC_GULV,
    NC_ULEVEL, NC_UTIME, NC_UPASS, NC_UDLIST, NC_UDEDIT, NC_SIGNUP, NC_AOFF,
    NC_AON, NC_SDTIME, NC_REPORT, NC_MESEDIT
} NetCommand;
typedef struct { char *word,*argument; } NetInput;
typedef enum { NC_MAIN_MENU, NC_USER_MENU, NC_BOARD_MENU, NC_MAIL_MENU, NC_CHAT_MENU, NC_REGISTER_MENU } NetMenu;

NetInput net_cock_split(char *);
NetCommand net_cock_command(const char *);
NetCommand net_cock_menu_command(NetMenu,unsigned,int authenticated,int administrator);
int net_cock_equal(const char *,const char *);
#endif
