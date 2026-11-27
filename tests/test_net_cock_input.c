#include "net_cock_input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char text[]=" \tBW  Free Board";
    NetInput input=net_cock_split(text);
    assert(!strcmp(input.word,"BW") && !strcmp(input.argument,"Free Board"));
    assert(net_cock_command("b")==NC_BREAD && net_cock_command("Bw")==NC_BWRITE);
    assert(net_cock_command("BA")==NC_BATCH && net_cock_command("bs")==NC_BSET);
    assert(net_cock_command("ra")==NC_RALL && net_cock_command("rn")==NC_RNALL);
    assert(net_cock_command("m")==NC_MODE && net_cock_command("MREAD")==NC_MREAD);
    assert(net_cock_command("ll")==NC_LLIST && net_cock_command("*")==NC_LLIST);
    assert(net_cock_command("BKI")==NC_UNKNOWN && net_cock_command("BKILL")==NC_BKILL);
    assert(net_cock_command("")==NC_UNKNOWN && net_cock_command("BREADOVER")==NC_UNKNOWN);
    assert(net_cock_menu_command(NC_MAIN_MENU,3,0,0)==NC_CHAT);
    assert(net_cock_menu_command(NC_MAIN_MENU,3,1,0)==NC_MREAD);
    assert(net_cock_menu_command(NC_MAIN_MENU,6,1,1)==NC_CHAT);
    assert(net_cock_menu_command(NC_MAIN_MENU,9,1,1)==NC_OFF);
    assert(net_cock_menu_command(NC_BOARD_MENU,7,1,0)==NC_PMOVE);
    assert(net_cock_menu_command(NC_BOARD_MENU,9,1,0)==NC_UNKNOWN);
    puts("PASS NET-COCK command priority, input boundaries and menu numbering");
    return 0;
}
