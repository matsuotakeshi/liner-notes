#include "userlog.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    assert(argc==2);char text[8192];assert(userlog_open(argv[1]));
    assert(userlog_connect(7,"TELNET",1000));assert(userlog_update(7,2,2000));
    assert(userlog_update(7,2,67000));
    userlog_report(2,0,0,1,69000,text,sizeof(text));assert(strstr(text,"LOGINS 1 SECONDS 67"));
    userlog_close(); /* Simulate process death after its 65-second checkpoint. */
    assert(userlog_open(argv[1]));userlog_report(2,0,0,1,900000,text,sizeof(text));
    assert(strstr(text,"LOGINS 1 SECONDS 65"));
    userlog_report(2,0,0,0,900000,text,sizeof(text));assert(strstr(text," X "));
    assert(userlog_connect(3,"RAW",1000000));assert(userlog_update(3,2,1000000));
    assert(userlog_connect(12,"TELNET",1001000));assert(userlog_update(12,2,1001000));
    assert(userlog_update(3,0,1010000));assert(userlog_disconnect(3,1012000));
    assert(userlog_disconnect(12,1014000));
    userlog_report(2,0,0,1,2000000,text,sizeof(text));assert(strstr(text,"LOGINS 3 SECONDS 88"));
    userlog_close();assert(userlog_open(argv[1]));
    userlog_report(2,0,0,1,3000000,text,sizeof(text));assert(strstr(text,"LOGINS 3 SECONDS 88"));
    userlog_close();puts("PASS: monotonic duration, checkpointed crash recovery, concurrent sessions and logout exclusion");return 0;
}
