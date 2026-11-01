#include "telnet.h"
#include <string.h>
enum { SE=240, SB=250, WILL=251, WONT=252, DO=253, DONT=254, IAC=255 };
enum { BINARY=0, ECHO=1, SGA=3, NAWS=31 };
enum { NO, REQUESTED, YES, REFUSED };
enum { DATA, COMMAND, OPTION, SUB_OPTION, SUB_DATA, SUB_IAC };
static int control(Telnet *t,int verb,int option) {
    unsigned char bytes[]={IAC,(unsigned char)verb,(unsigned char)option};
    return t->write(t->context,bytes,sizeof(bytes));
}
int telnet_init(Telnet *t,ByteSink write,void *context) {
    memset(t,0,sizeof(*t)); t->write=write; t->context=context;
    const int ours[]={ECHO,SGA,BINARY},theirs[]={SGA,BINARY,NAWS};
    for(size_t i=0;i<3;i++) { t->local[ours[i]]=REQUESTED; if(!control(t,WILL,ours[i])) return 0; }
    for(size_t i=0;i<3;i++) { t->remote[theirs[i]]=REQUESTED; if(!control(t,DO,theirs[i])) return 0; }
    return 1;
}
/* Preferences are fixed for the connection. No queued enable/disable requests;
 * pending acknowledgements and duplicate requests must not form reply loops. */
static int negotiate(Telnet *t,int verb,unsigned option) {
    int local=verb==DO || verb==DONT;
    unsigned char *state=local?&t->local[option]:&t->remote[option];
    int positive=verb==DO || verb==WILL;
    int allowed=option==BINARY || option==SGA || (local?option==ECHO:option==NAWS);
    if(positive) {
        if(!allowed) { if(*state!=REFUSED && !control(t,local?WONT:DONT,(int)option)) return 0; *state=REFUSED; }
        else { if(*state!=YES && *state!=REQUESTED && !control(t,local?WILL:DO,(int)option)) return 0; *state=YES; }
    } else {
        if(*state==YES && !control(t,local?WONT:DONT,(int)option)) return 0;
        *state=NO;
        if(!local && option==NAWS) t->width=t->height=0;
    }
    return 1;
}
static int sub_byte(Telnet *t,unsigned char byte) {
    if(t->sub_size>=1024) return 0;
    if(t->sub_size<sizeof(t->sub)) t->sub[t->sub_size]=byte;
    t->sub_size++; return 1;
}
int telnet_receive(Telnet *t,unsigned char byte,unsigned char *data) {
    switch(t->state) {
    case DATA:
        if(byte==IAC) { t->state=COMMAND; return TN_NONE; }
        *data=byte; return TN_DATA;
    case COMMAND:
        t->state=DATA;
        if(byte==IAC) { *data=IAC; return TN_DATA; }
        if(byte>=WILL && byte<=DONT) { t->verb=byte; t->state=OPTION; }
        else if(byte==SB) { t->state=SUB_OPTION; t->sub_size=0; }
        else if(byte==247) return TN_ERASE;
        else if(byte==248) return TN_KILL;
        return TN_NONE;
    case OPTION:
        t->state=DATA; return negotiate(t,t->verb,byte)?TN_NONE:-1;
    case SUB_OPTION: t->option=byte; t->state=SUB_DATA; return TN_NONE;
    case SUB_DATA:
        if(byte==IAC) { t->state=SUB_IAC; return TN_NONE; }
        return sub_byte(t,byte)?TN_NONE:-1;
    case SUB_IAC:
        if(byte==IAC) { t->state=SUB_DATA; return sub_byte(t,byte)?TN_NONE:-1; }
        if(byte!=SE) return -1;
        t->state=DATA;
        if(t->option==NAWS && t->remote[NAWS]==YES && t->sub_size==4) {
            t->width=(unsigned)t->sub[0]*256+t->sub[1]; t->height=(unsigned)t->sub[2]*256+t->sub[3];
        }
        return TN_NONE;
    }
    return -1;
}
int telnet_binary(const Telnet *t) { return t->local[BINARY]==YES && t->remote[BINARY]==YES; }
int telnet_echo(const Telnet *t) { return t->local[ECHO]==YES; }
int telnet_write(Telnet *t,const unsigned char *data,size_t size) {
    unsigned char out[512]; size_t used=0;
    for(size_t i=0;i<size;i++) {
        if(used>sizeof(out)-4) { if(!t->write(t->context,out,used)) return 0; used=0; }
        unsigned char b=data[i];
        if(b==IAC) { out[used++]=IAC; out[used++]=IAC; }
        else if(t->local[BINARY]!=YES && b=='\r') {
            out[used++]='\r'; out[used++]=i+1<size && data[i+1]=='\n'?'\n':0;
            if(out[used-1]=='\n') i++;
        } else if(t->local[BINARY]!=YES && b=='\n') { out[used++]='\r'; out[used++]='\n'; }
        else out[used++]=b;
    }
    return !used || t->write(t->context,out,used);
}
