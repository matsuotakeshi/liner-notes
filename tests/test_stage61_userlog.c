#include "userlog.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    assert(argc==2);char before[8192],after[8192],table[8192];userlog_keep=3;assert(userlog_open(argv[1]));
    assert(userlog_connect(7,"TELNET",1000));assert(userlog_update(7,1,1000));
    for(unsigned n=0;n<30;n++) { unsigned slot=n%3;uint64_t now=2000+n*2000;
        assert(userlog_connect((int)slot,"RAW",now));assert(userlog_update((int)slot,2,now));assert(userlog_disconnect((int)slot,now+1000));
        userlog_report(2,0,0,1,now+1000,before,sizeof(before));assert(userlog_maintain());userlog_report(2,0,0,1,now+1000,after,sizeof(after));assert(!strcmp(before,after));
        assert(userlog_update(7,1,now+1000));
    }
    assert(strstr(after,"LOGINS 30 SECONDS 30"));assert(userlog_disconnect(7,70000));assert(userlog_maintain());
    userlog_table(2,0,0,70000,table,sizeof(table));assert(strstr(table," 30 RAW\r\n"));
    userlog_report(1,0,0,1,70000,before,sizeof(before));assert(strstr(before,"LOGINS 1 SECONDS 69"));
    userlog_close();assert(userlog_open(argv[1]));userlog_report(1,0,0,1,70000,after,sizeof(after));assert(!strcmp(before,after));
    userlog_report(2,0,0,1,70000,after,sizeof(after));assert(strstr(after,"LOGINS 30 SECONDS 30"));userlog_close();puts("PASS retained complete connections, concurrent slots, lifetime totals and restart");return 0;
}
