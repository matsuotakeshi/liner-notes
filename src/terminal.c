#include "terminal.h"
#include <string.h>
#include <openssl/crypto.h>
int terminal_init(Terminal *t,TerminalMode mode,ByteSink write,void *context) {
    memset(t,0,sizeof(*t)); t->mode=mode; t->write=write; t->context=context;
    return mode!=TERMINAL_TELNET || telnet_init(&t->telnet,write,context);
}
int terminal_write(Terminal *t,const unsigned char *data,size_t size) {
    if(t->binary && !terminal_binary_ready(t))return 0;
    return t->mode==TERMINAL_TELNET?telnet_write(&t->telnet,data,size):t->write(t->context,data,size);
}
void terminal_clear(Terminal *t) { OPENSSL_cleanse(t->line,sizeof(t->line)); t->size=0; }
void terminal_dispose(Terminal *t) { OPENSSL_cleanse(t,sizeof(*t)); }
static unsigned cells(const unsigned char *p,size_t n) {
    unsigned cp=p[0];
    if(n>=2 && (p[0]&0xe0)==0xc0) cp=((p[0]&31)<<6)|(p[1]&63);
    else if(n>=3 && (p[0]&0xf0)==0xe0) cp=((p[0]&15)<<12)|((p[1]&63)<<6)|(p[2]&63);
    else if(n>=4 && (p[0]&0xf8)==0xf0) cp=((p[0]&7)<<18)|((p[1]&63)<<12)|((p[2]&63)<<6)|(p[3]&63);
    /* Approximate terminal cell width; grapheme clusters are not edited here. */
    return (cp>=0x1100 && (cp<=0x115f || (cp>=0x2e80 && cp<=0xa4cf) ||
        (cp>=0xac00 && cp<=0xd7a3) || (cp>=0xf900 && cp<=0xfaff) ||
        (cp>=0xfe10 && cp<=0xfe6f) || (cp>=0xff01 && cp<=0xff60) ||
        (cp>=0x1f300 && cp<=0x1faff) || cp>=0x20000))?2:1;
}
static int erase(Terminal *t,int echo) {
    if(!t->size) return 1;
    size_t start=t->size-1;
    while(start && ((unsigned char)t->line[start]&0xc0)==0x80) start--;
    unsigned width=cells((unsigned char *)t->line+start,t->size-start);
    OPENSSL_cleanse(t->line+start,t->size-start); t->size=start;
    const unsigned char rubout[]="\b \b\b \b";
    return !echo || terminal_write(t,rubout,width*3);
}
int terminal_receive(Terminal *t,unsigned char byte,int secret) {
    int event=TN_DATA;
    if(t->mode==TERMINAL_TELNET) { event=telnet_receive(&t->telnet,byte,&byte); if(event<=0) return event; }
    if(t->mode==TERMINAL_RAW) {
        if(byte=='\n') { if(t->size && t->line[t->size-1]=='\r') t->line[--t->size]=0; return 1; }
        if(!byte || t->size>=INPUT_LINE_MAX+1 || (t->size==INPUT_LINE_MAX && byte!='\r')) return -1;
        t->line[t->size++]=(char)byte; return 0;
    }
    int echo=!secret && (t->mode==TERMINAL_PLAIN || telnet_echo(&t->telnet));
    if(event==TN_ERASE) return erase(t,echo)?0:-1;
    if(event==TN_KILL || byte==21) { while(t->size) if(!erase(t,echo)) return -1; return 0; }
    if(t->after_cr) { t->after_cr=0; if(byte=='\n' || byte==0) return 0; }
    /* Ignore bounded ANSI cursor-key sequences, never turn them into commands. */
    if(t->escape) {
        if(++t->escape_size>32) return -1;
        if(t->escape==1) t->escape=(byte=='[' || byte=='O')?2:0;
        else if(byte>=0x40 && byte<=0x7e) t->escape=0;
        return 0;
    }
    if(byte==27) { t->escape=1; t->escape_size=0; return 0; }
    if(byte==8 || byte==127) return erase(t,echo)?0:-1;
    if(byte=='\r' || byte=='\n') {
        t->after_cr=byte=='\r';
        /* A newline is not secret data; keep the next prompt on a new line. */
        if((t->mode==TERMINAL_PLAIN || telnet_echo(&t->telnet)) && !terminal_write(t,(const unsigned char *)"\r\n",2)) return -1;
        return 1;
    }
    if(byte==3) { terminal_clear(t);memcpy(t->line,"STOP",4);t->size=4;return 1; }
    if(!byte || byte<32) return 0;
    if(t->size>=INPUT_LINE_MAX) return -1;
    if(t->uppercase && !secret && byte>='a' && byte<='z')byte=(unsigned char)(byte-'a'+'A');
    t->line[t->size++]=(char)byte;
    return !echo || terminal_write(t,&byte,1)?0:-1;
}

int terminal_binary_ready(const Terminal *t) { return t->mode!=TERMINAL_TELNET || telnet_binary(&t->telnet); }
void terminal_set_binary(Terminal *t,int enabled) {
    if(t->binary==enabled)return;
    t->binary=enabled;terminal_clear(t);t->escape=0;t->escape_size=0;
    if(!enabled)t->after_cr=0;
}
int terminal_binary_receive(Terminal *t,unsigned char byte,unsigned char *out) {
    int event=TN_DATA;
    if(t->mode==TERMINAL_TELNET)event=telnet_receive(&t->telnet,byte,&byte);
    if(event<0 || !terminal_binary_ready(t))return -1;
    if(event!=TN_DATA)return 0;
    /* Entry's CRLF/CRNUL may have been buffered while authorization ran.
     * Frames always start with nonzero '~', so this cannot eat a header byte. */
    if(t->after_cr) { t->after_cr=0;if(byte=='\n' || !byte)return 0; }
    *out=byte;return 1;
}
