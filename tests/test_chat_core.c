#include "chat_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { ChatEvent events[512];unsigned count;int reject; } Sink;
typedef struct { ChatEvent last;unsigned writes;int fail,enabled;Sink *observe;unsigned before; } Store;
static ChatCore first,second;
static Sink alice,bob,carol;
static int notify(void *context,const ChatEvent *event) {
    Sink *sink=context;if(sink->reject)return 0;
    assert(sink->count<512);sink->events[sink->count++]=*event;return 1;
}
static int save(void *context,const ChatEvent *event) {
    Store *store=context;assert(event->kind=='S');
    if(store->observe)assert(store->observe->count==store->before); /* durable before delivery */
    store->writes++;store->last=*event;return !store->fail;
}
static int enabled(void *context) { return ((Store *)context)->enabled; }
static void reset(void) {
    memset(&first,0,sizeof(first));memset(&second,0,sizeof(second));
    memset(&alice,0,sizeof(alice));memset(&bob,0,sizeof(bob));memset(&carol,0,sizeof(carol));
}
static void isolation(void) {
    reset();ChatPreferences prefs={0};
    assert(!chat_core_register(&first,1,0,"guest","Guest",notify,&alice,&prefs));
    assert(chat_core_join(&first,1,1,"alice","Alice",notify,&alice));
    assert(chat_core_join(&second,1,2,"bob","Bob",notify,&bob)); /* same connection ID, different core */
    assert(!chat_core_register(&first,1,2,"bob","Bob",notify,&bob,&prefs));
    alice.count=bob.count=0;
    assert(chat_core_say(&first,1,"only first",1000)==1);
    assert(alice.count==1 && bob.count==0);
    assert(chat_core_say(&second,1,"only second",1000)==1);
    assert(bob.count==1 && alice.count==1);
    assert(chat_core_say(&first,1,"fast",1100)==-1);
    chat_core_disconnect(&first,1);
    assert(!chat_core_say(&first,1,"stale",2000));
    assert(chat_core_say(&second,1,"still online",2000)==1);
    chat_core_disconnect(&second,1);
    assert(!first.history_count && !second.history_count);
}
static void persistence(void) {
    reset();Store store={0};store.enabled=1;
    first.persistence=(ChatPersistence){&store,save,enabled};
    assert(chat_core_join(&first,1,1,"alice","Alice",notify,&alice));
    store.observe=&alice;store.before=alice.count;store.fail=1;
    unsigned count=first.history_count;
    assert(chat_core_say(&first,1,"not committed",1000)==-4);
    assert(first.history_count==count && alice.count==store.before && store.writes==1);
    assert(chat_core_say(&first,1,"rate still applies",1100)==-1);
    store.fail=0;assert(chat_core_say(&first,1,"committed",1500)==1);
    assert(store.writes==2 && !strcmp(store.last.text,"committed"));
    assert(alice.count==store.before+1);
    chat_core_leave(&first,1);assert(first.history_count==1 && first.history[0].kind=='S');
    assert(chat_core_restore_public(&second,&store.last));assert(!bob.count);
    assert(chat_core_join(&second,2,2,"bob","Bob",notify,&bob));
    assert(bob.count==2 && bob.events[0].replay && bob.events[0].connection==0);
    assert(!strcmp(bob.events[0].text,"committed"));
    ChatEvent invalid=store.last;invalid.kind='I';assert(!chat_core_restore_public(&second,&invalid));
    invalid.kind='O';assert(!chat_core_restore_public(&second,&invalid));
    invalid.kind='S';invalid.user=0;assert(!chat_core_restore_public(&second,&invalid));
    invalid.user=1;strcpy(invalid.text,"\033bad");assert(!chat_core_restore_public(&second,&invalid));
    invalid=store.last;memset(invalid.name,'x',sizeof(invalid.name));assert(!chat_core_restore_public(&second,&invalid));
    chat_core_leave(&second,2);assert(!second.history_count);
    memset(&second,0,sizeof(second));
    for(unsigned i=0;i<CHAT_HISTORY+5;i++) {
        ChatEvent event=store.last;snprintf(event.text,sizeof(event.text),"%u",i);
        assert(chat_core_restore_public(&second,&event));
    }
    assert(second.history_count==CHAT_HISTORY && !strcmp(second.history[0].text,"5"));
}
static void privacy_and_preferences(void) {
    reset();ChatPreferences prefs={0};Store store={0};first.persistence=(ChatPersistence){&store,save,enabled};
    assert(chat_core_register(&first,1,1,"alice","Alice",notify,&alice,&prefs));
    assert(chat_core_register(&first,2,2,"bob","Bob",notify,&bob,&prefs));
    assert(chat_core_register(&first,3,2,"bob","Bob2",notify,&carol,&prefs));
    assert(chat_core_private(&first,1,2,"private",1000)==1);
    assert(alice.count==1 && bob.count==1 && !carol.count && !store.writes && !first.history_count);
    assert(alice.events[0].kind=='O' && bob.events[0].kind=='I');
    prefs.revision=2;prefs.deny=1;chat_core_apply_preferences(&first,2,&prefs);
    assert(chat_core_private(&first,1,2,"denied",2000)==-3);
    assert(chat_core_private(&first,1,3,"all connections",2000)==-3);
    ChatPreferences stale={0};stale.revision=1;chat_core_apply_preferences(&first,2,&stale);
    assert(chat_core_private(&first,1,2,"stale",2000)==-3);
    prefs.revision=3;prefs.deny=0;chat_core_apply_preferences(&first,2,&prefs);
    assert(chat_core_mute(&first,2,"alice",1));assert(chat_core_private(&first,1,2,"muted",2000)==-3);
    assert(chat_core_mute(&first,2,"alice",0));
    bob.reject=1;assert(chat_core_private(&first,1,2,"busy",2000)==-2);assert(alice.count==1);
    bob.reject=0;assert(chat_core_private(&first,1,2,"retry without rate penalty",2000)==1);
    assert(chat_core_private(&first,1,2,"rate",2100)==-1);
    assert(chat_core_join_config(&first,1,1,"alice","Alice",notify,&alice,&prefs));
    assert(chat_core_say(&first,1,"public shares rate",2200)==-1);
    assert(chat_core_say(&first,1,"public",2500)==1);assert(store.writes==1);
    assert(chat_core_private(&first,1,2,"private shares rate",2600)==-1);
    assert(chat_core_private(&first,2,1,"independent sender",2600)==1);
    for(unsigned i=0;i<first.history_count;i++)assert(first.history[i].kind!='I' && first.history[i].kind!='O');
    chat_core_leave(&first,1);assert(chat_core_target(&first,1,2)); /* outside room still registered */
    chat_core_disconnect(&first,2);assert(!chat_core_target(&first,1,2));
    assert(chat_core_private(&first,1,2,"gone",3000)==-2);
}
static void presence_and_call(void) {
    reset();ChatPreferences prefs={0};
    assert(chat_core_register(&first,1,1,"alice","Alice",notify,&alice,&prefs));
    assert(chat_core_register(&first,2,2,"bob","Bob",notify,&bob,&prefs));
    assert(chat_core_register(&first,3,3,"carol","Carol",notify,&carol,&prefs));
    chat_core_admin(&first,2,1);chat_core_admin(&first,3,1);carol.reject=1;
    assert(chat_core_call(&first,1,"help",1000)==1);
    assert(!alice.count && bob.count==1 && !carol.count && bob.events[0].kind=='C');
    assert(chat_core_call(&first,1,"too soon",30999)==-1);
    assert(chat_core_call(&first,1,"ready",31000)==1);
    assert(!chat_core_memo(&first,1,"\033bad"));assert(chat_core_memo(&first,1,"接続者メモ"));
    chat_core_online(&first,1);assert(alice.count==4 && alice.events[3].kind=='E');
    assert(!strcmp(alice.events[0].text,"接続者メモ"));
    alice.count=0;chat_core_who(&first,1);assert(alice.count==1 && alice.events[0].kind=='E');
    alice.count=0;chat_core_disconnect(&first,2);assert(alice.count==1 && alice.events[0].kind=='X');
}
static void presence_snapshots(void) {
    ChatCore core={0};ChatPreferences prefs={0};Sink a={0},b={0};
    assert(chat_core_join(&core,7,1,"alice","Alice",notify,&a));
    assert(chat_core_register(&core,9,2,"bob","Bob",notify,&b,&prefs));
    assert(chat_core_memo(&core,9,"memo"));
    ChatPresence presence;
    assert(!chat_core_presence(&core,7,2,0,&presence) && !presence.count);
    assert(!chat_core_presence(&core,9,2,1,&presence) && !presence.count);
    assert(chat_core_presence(&core,7,1,1,&presence) && presence.count==1);
    assert(presence.rows[0].connection==7 && !strcmp(presence.rows[0].login,"alice"));
    assert(chat_core_presence(&core,7,1,0,&presence) && presence.count==2);
    assert(!strcmp(presence.rows[1].memo,"memo"));
}

static void bound_telegram(void) {
    reset();ChatPreferences prefs={0};
    assert(chat_core_register(&first,1,1,"alice","Alice",notify,&alice,&prefs));
    assert(chat_core_register(&first,2,2,"bob","Bob",notify,&bob,&prefs));
    ChatPresence peers;
    assert(chat_core_presence(&first,1,1,0,&peers) && peers.count==2);
    uint64_t old=peers.rows[1].generation;
    assert(old && peers.rows[1].accepts_telegram);
    chat_core_disconnect(&first,2);
    assert(chat_core_register(&first,2,3,"carol","Carol",notify,&carol,&prefs));
    assert(chat_core_private_scoped(&first,1,2,old,"never deliver",1000)==-2);
    assert(!carol.count);
    assert(chat_core_presence(&first,1,1,0,&peers) && peers.rows[1].generation!=old);
    uint64_t current=peers.rows[1].generation;
    assert(chat_core_receive(&first,2,0));
    assert(chat_core_presence(&first,1,1,0,&peers) && !peers.rows[1].accepts_telegram);
    assert(chat_core_private_scoped(&first,1,2,current,"refused",1000)==-3);
    assert(chat_core_receive(&first,2,1));
    assert(chat_core_private_scoped(&first,1,2,current,"deliver",1000)==1);
    assert(carol.count==1 && !strcmp(carol.events[0].text,"deliver"));
}

int main(void) {
    bound_telegram();presence_snapshots();
    isolation();persistence();privacy_and_preferences();presence_and_call();
    puts("PASS chat core: context isolation, durable-before-delivery, replay, private routing, preferences, presence, SYSOP cooldown");
    return 0;
}
