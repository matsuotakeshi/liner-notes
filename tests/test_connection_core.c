#include "connection_core.h"
#include "connection_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void time_limits(void) {
    ConnectionPolicy p={.level_seconds={3,0,10,0,0,0},.warning_seconds=2,.grace_seconds=1};
    ConnectionClock c={.started=1000};ConnectionState s={.user=1,.level=2};
    assert(connection_time_check(&p,&c,&s,8999)==CONNECTION_NONE);
    assert(connection_time_check(&p,&c,&s,9000)==CONNECTION_WARNING);
    assert(connection_time_check(&p,&c,&s,9500)==CONNECTION_NONE);
    assert(connection_time_check(&p,&c,&s,11000)==CONNECTION_GRACE);
    assert(connection_time_check(&p,&c,&s,11999)==CONNECTION_NONE);
    assert(connection_time_check(&p,&c,&s,12000)==CONNECTION_DROP);
    s.admin=1;assert(connection_time_check(&p,&c,&s,999999)==CONNECTION_NONE);
    s.admin=0;s.closing=1;assert(connection_time_check(&p,&c,&s,999999)==CONNECTION_NONE);
    s.closing=0;s.level=1;assert(connection_time_check(&p,&c,&s,999999)==CONNECTION_NONE);
    s.user=0;s.level=5;assert(connection_time_check(&p,&c,&s,5000)==CONNECTION_DROP);
    s.user=1;s.level=99;assert(connection_time_check(&p,&c,&s,5000)==CONNECTION_DROP);
    s.level=2;
    for(unsigned mode=0;mode<4;mode++) {
        ConnectionClock blocked={.started=1000};s.door=mode==0;s.zmodem=mode==1;s.binary=mode==2;s.secret=mode==3;
        assert(connection_time_check(&p,&blocked,&s,9000)==CONNECTION_NONE);
        assert(connection_time_check(&p,&blocked,&s,11000)==CONNECTION_NONE);
        assert(connection_time_check(&p,&blocked,&s,12000)==CONNECTION_DROP);
        assert(!blocked.time_warning && !blocked.time_grace);
    }
    s=(ConnectionState){.user=1,.level=2,.pending=1,.editing=1};
    c=(ConnectionClock){.started=1000};assert(connection_time_check(&p,&c,&s,9000)==CONNECTION_WARNING);
    p.grace_seconds=0;c=(ConnectionClock){.started=1000};
    assert(connection_time_check(&p,&c,&s,11000)==CONNECTION_DROP);
    p.grace_seconds=1;c=(ConnectionClock){.started=1000};s.secret=1;
    assert(connection_time_check(&p,&c,&s,500)==CONNECTION_NONE); /* clock rollback clamps elapsed */
}
static void idle_limits(void) {
    ConnectionPolicy p={.idle_seconds=10};ConnectionClock c={.last_input=1000};ConnectionState s={.menu=1};
    assert(connection_idle_check(&p,&c,&s,5999)==CONNECTION_NONE);
    assert(connection_idle_check(&p,&c,&s,6000)==CONNECTION_WARNING);
    assert(connection_idle_check(&p,&c,&s,6500)==CONNECTION_NONE);
    connection_input(&c,7000);assert(!c.idle_warned && c.last_input==7000);
    assert(connection_idle_check(&p,&c,&s,11999)==CONNECTION_NONE);
    assert(connection_idle_check(&p,&c,&s,12000)==CONNECTION_WARNING);
    assert(connection_idle_check(&p,&c,&s,17000)==CONNECTION_CLOSE);
    assert(c.idle_close==18000 && !connection_idle_drained(&c,17999) && connection_idle_drained(&c,18000));
    for(unsigned mode=0;mode<5;mode++) {
        c=(ConnectionClock){0};s=(ConnectionState){.admin=mode==0,.closing=mode==1,.pending=mode==2,.binary=mode==3,.zmodem=mode==4};
        assert(connection_idle_check(&p,&c,&s,999999)==CONNECTION_NONE);assert(!c.idle_close);
    }
    for(unsigned mode=0;mode<2;mode++) {
        c=(ConnectionClock){0};s=(ConnectionState){.menu=mode==1,.secret=mode==1};
        assert(connection_idle_check(&p,&c,&s,5000)==CONNECTION_NONE);
        assert(!c.idle_warned && connection_idle_check(&p,&c,&s,10000)==CONNECTION_CLOSE);
    }
    s=(ConnectionState){.menu=1,.door=1};c=(ConnectionClock){0};
    assert(connection_idle_check(&p,&c,&s,10000)==CONNECTION_CLOSE); /* Door is not idle exemption */
    for(unsigned seconds=120;seconds<=121;seconds++) {
        p.idle_seconds=seconds;c=(ConnectionClock){0};s=(ConnectionState){.menu=1};
        uint64_t at=seconds==120?60000:61000;
        assert(connection_idle_check(&p,&c,&s,at-1)==CONNECTION_NONE);
        assert(connection_idle_check(&p,&c,&s,at)==CONNECTION_WARNING);
    }
    p.idle_seconds=0;assert(connection_idle_check(&p,&c,&s,999999)==CONNECTION_NONE);
    for(unsigned mode=0;mode<3;mode++) {
        c=(ConnectionClock){.idle_warned=1};s=(ConnectionState){.pending=mode==0,.binary=mode==1,.zmodem=mode==2};
        connection_busy_activity(&c,&s,12345);assert(c.last_input==12345 && !c.idle_warned);
    }
    ConnectionClock other={.last_input=8};connection_input(&c,555);assert(other.last_input==8);
}
static void shutdowns(void) {
    ConnectionShutdown state={0};assert(connection_shutdown_step(&state,1000)==SHUTDOWN_WAIT);
    connection_shutdown_schedule(&state,1000,2,"maintenance");assert(state.at==3000 && !state.drain);
    assert(connection_shutdown_remaining(&state,1001)==2 && connection_shutdown_remaining(&state,2999)==1);
    assert(connection_shutdown_step(&state,2999)==SHUTDOWN_WAIT);
    assert(connection_shutdown_step(&state,3000)==SHUTDOWN_DRAIN);
    assert(state.drain==3500 && connection_shutdown_remaining(&state,3000)==0);
    assert(connection_shutdown_step(&state,3499)==SHUTDOWN_WAIT);
    assert(connection_shutdown_step(&state,3500)==SHUTDOWN_STOP);
    connection_shutdown_cancel(&state);assert(!state.at && !state.drain && !*state.reason);
    assert(connection_shutdown_step(&state,9999)==SHUTDOWN_WAIT);
    connection_shutdown_schedule(&state,5000,0,"now");assert(connection_shutdown_step(&state,5000)==SHUTDOWN_DRAIN);
    connection_shutdown_schedule(&state,5001,10,"replaced");assert(state.at==15001 && !state.drain);
    assert(!strcmp(state.reason,"replaced") && connection_shutdown_step(&state,6000)==SHUTDOWN_WAIT);
}
static void reports(void) {
    ConnectionState s={.door=1,.zmodem=1,.binary=1,.secret=1,.chat=1,.editing=1};
    assert(connection_mode(&s)==CONNECTION_DOOR);s.door=0;
    assert(connection_mode(&s)==CONNECTION_ZMODEM);s.zmodem=0;
    assert(connection_mode(&s)==CONNECTION_BINARY);s.binary=0;
    assert(connection_mode(&s)==CONNECTION_AUTH);s.secret=0;
    assert(connection_mode(&s)==CONNECTION_CHAT);s.chat=0;
    assert(connection_mode(&s)==CONNECTION_EDITOR);s.editing=0;
    assert(connection_mode(&s)==CONNECTION_BBS);
    ConnectionSummary rows[2]={{.id=7,.mode=CONNECTION_DOOR,.telnet=1,.login="alice",.peer="127.0.0.1:1234",.elapsed=9,.idle=2,.rx=4294967296ULL,.tx=8,.queued=3,.columns=80,.rows=24},
        {.id=8,.mode=CONNECTION_AUTH,.login="guest",.columns=100,.rows=30}};
    char out[1024];assert(connection_protocol_list(rows,2,out,sizeof(out)));
    assert(!strcmp(out,"CONNECTIONS\r\nCONNECTION 7 alice DOOR TELNET 127.0.0.1:1234 9 2 4294967296 8 3 80 24\r\nCONNECTION 8 guest AUTH RAW unknown 0 0 0 0 0 100 30\r\nEND CONNECTIONS\r\n"));
    size_t length=strlen(out);
    for(size_t size=0;size<=length;size++) {
        char guarded[1026];memset(guarded,'!',sizeof(guarded));
        assert(!connection_protocol_list(rows,2,guarded+1,size));
        assert(guarded[0]=='!' && guarded[size+1]=='!');if(size)assert(!guarded[1]);
    }
    assert(connection_protocol_list(rows,2,out,length+1));
    assert(connection_protocol_list(NULL,0,out,sizeof(out)) && !strcmp(out,"CONNECTIONS\r\nEND CONNECTIONS\r\n"));
    rows[0].mode=(ConnectionMode)99;assert(!connection_protocol_list(rows,1,out,sizeof(out)) && !*out);
    assert(connection_elapsed(10,20)==0 && connection_elapsed(2000,1000)==1000);
}
int main(void) {
    time_limits();idle_limits();shutdowns();reports();
    puts("PASS connection core: deadlines, exemptions, notices, input activity, shutdown/cancel, legacy report and bounds");return 0;
}
