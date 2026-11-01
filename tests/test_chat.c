#include "chat.h"
#include "app_limits.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static ChatEvent seen[CONNECTION_MAX+CHAT_HISTORY+32];static unsigned n;
static int notify(void *context,const ChatEvent *e) { (void)context;assert(n<sizeof(seen)/sizeof(seen[0]));seen[n++]=*e;return 1; }
int main(void) {
    assert(chat_text_valid("日本語"));assert(!chat_text_valid("\xc0\xaf"));assert(!chat_text_valid("\xed\xa0\x80"));
    assert(!chat_text_valid("\xc2\x9b"));assert(!chat_text_valid("\033[2J"));
    assert(chat_join(1,1,"alice","Alice",notify,NULL));
    assert(chat_say(1,"hello",1000)==1);assert(chat_say(1,"fast",1200)==-1);
    for(int i=0;i<25;i++)assert(chat_say(1,"history",2000+(uint64_t)i*500)==1);
    n=0;assert(chat_join(2,2,"bob","Bob",notify,NULL));
    assert(n==CHAT_HISTORY+2);for(unsigned i=0;i<CHAT_HISTORY;i++)assert(seen[i].replay && seen[i].kind=='S');
    n=0;chat_who(2);assert(n==3 && seen[0].kind=='W' && seen[2].kind=='E');
    chat_leave(1);chat_leave(2);n=0;assert(chat_join(1,1,"alice","Alice",notify,NULL));assert(n==1);
    chat_leave(1);assert(!chat_say(1,"stale",99999));
    puts("PASS: independent chat room, UTF-8/control validation, rate bound, 20-event history, participants and empty-room cleanup");
    n=0;assert(chat_join(10,1,"alice","Alice",notify,NULL));assert(chat_join(20,2,"bob","Bob",notify,NULL));
    assert(chat_join(30,3,"carol","Carol",notify,NULL));n=0;
    assert(chat_private(10,20,"private",10000)==1);assert(n==2 && seen[0].kind=='I' && seen[1].kind=='O');
    assert(chat_private(10,20,"too fast",10100)==-1);assert(chat_say(10,"too fast",10200)==-1);
    assert(chat_private(10,10,"self",11000)==-2);assert(chat_private(10,99,"missing",11000)==-2);
    n=0;assert(chat_join(40,4,"dave","Dave",notify,NULL));
    for(unsigned i=0;i<n;i++)assert(seen[i].kind!='I' && seen[i].kind!='O');
    n=0;assert(chat_receive(20,0));assert(chat_private(10,20,"refused",12000)==-3);assert(n==0);
    assert(chat_receive(20,1));assert(chat_mute(20,"alice",1));
    assert(!chat_mute(20,"bob",1));assert(!chat_mute(20,"missing",1));
    assert(chat_private(10,20,"muted",12000)==-3);assert(n==0);
    assert(chat_say(10,"muted public",12000)==1);assert(n==3);
    assert(chat_mute(20,"alice",0));assert(chat_private(10,20,"unmuted",13000)==1);
    n=0;chat_settings(20);assert(n==2 && seen[0].kind=='A' && !strcmp(seen[0].text,"ON") && seen[1].kind=='E');
    chat_leave(20);assert(!chat_target(10,20));assert(chat_private(10,20,"stale",12000)==-2);
    chat_leave(10);chat_leave(30);chat_leave(40);
    puts("PASS: private routing has only two recipients, no shared history, shared rate limit and no stale target fallback");
    n=0;ChatPreferences prefs={0};prefs.revision=2;prefs.deny=1;
    assert(chat_join_config(50,1,"alice","Alice",notify,NULL,&prefs));
    assert(chat_join_config(51,1,"alice","Alice",notify,NULL,&prefs));
    assert(chat_join(60,2,"bob","Bob",notify,NULL));n=0;
    assert(chat_private(60,50,"refused",20000)==-3);assert(chat_private(60,51,"refused",20000)==-3);assert(n==0);
    ChatPreferences stale={0};stale.revision=1;chat_apply_preferences(1,&stale);
    assert(chat_private(60,50,"stale cannot enable",20000)==-3);
    prefs.revision=3;prefs.deny=0;chat_apply_preferences(1,&prefs);
    assert(chat_private(60,51,"enabled",20000)==1);
    chat_leave(50);chat_leave(51);chat_leave(60);
    puts("PASS: account snapshots update all peers and reject stale settings revisions");
    ChatPreferences clean={0};
    for(unsigned i=0;i<CONNECTION_MAX;i++)assert(chat_register(1000+i,i+1,"test","Test",notify,NULL,&clean));
    assert(!chat_register(9999,1,"test","Test",notify,NULL,&clean));
    for(unsigned i=0;i<CONNECTION_MAX;i++) { n=0;chat_disconnect(1000+i); }
    puts("PASS: 64 registered chat peers; overflow refused and peers cleaned up");
    return 0;
}
