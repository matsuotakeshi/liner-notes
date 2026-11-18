#include "terminal_pager.h"
#include <string.h>
TerminalDimensions terminal_dimensions(unsigned columns,unsigned rows) {
    return (TerminalDimensions){columns>=20 && columns<=240?columns:80,rows>=5 && rows<=100?rows:24};
}
/* Count common UTF-8 terminal cells without relying on the process locale. */
static unsigned cell_width(const unsigned char *p,size_t *bytes) {
    unsigned cp=p[0]; *bytes=1;
    unsigned n=cp>=0xf0 && cp<=0xf4?4:cp>=0xe0 && cp<=0xef?3:cp>=0xc2 && cp<=0xdf?2:1;
    if(n>1) {
        cp&=(1u<<(7-n))-1;
        for(unsigned i=1;i<n;i++) { if(!p[i] || (p[i]&0xc0)!=0x80) return 1; cp=(cp<<6)|(p[i]&63); }
        *bytes=n;
    }
    if((cp>=0x300 && cp<=0x36f) || (cp>=0xfe00 && cp<=0xfe0f) || cp==0x200d) return 0;
    return (cp>=0x1100 && (cp<=0x115f || cp==0x2329 || cp==0x232a ||
            (cp>=0x2e80 && cp<=0xa4cf) || (cp>=0xac00 && cp<=0xd7a3) ||
            (cp>=0xf900 && cp<=0xfaff) || (cp>=0xfe10 && cp<=0xfe6f) ||
            (cp>=0xff01 && cp<=0xff60) || (cp>=0xffe0 && cp<=0xffe6) || cp>=0x1f300))?2:1;
}
int terminal_pager_render(const char *text,size_t *offset,unsigned columns,unsigned rows,char *out,size_t capacity) {
    if(capacity<5) { if(capacity)*out=0;return -1; }
    TerminalDimensions size=terminal_dimensions(columns,rows);
    size_t used=0;unsigned lines=0,col=0,limit=size.rows-3,width=size.columns-1;
    while(text[*offset] && lines<limit) {
        const unsigned char *p=(const unsigned char *)text+*offset;
        if(*p=='\r' || *p=='\n') {
            size_t n=(*p=='\r' && p[1]=='\n')?2:1;
            if(used+2>=capacity)break;
            out[used++]='\r';out[used++]='\n';*offset+=n;lines++;col=0;continue;
        }
        size_t n;unsigned cells=cell_width(p,&n);
        if(col+cells>width) {
            if(used+2>=capacity)break;
            out[used++]='\r';out[used++]='\n';lines++;col=0;continue;
        }
        if(used+n>=capacity)break;
        memcpy(out+used,p,n);used+=n;*offset+=n;col+=cells;
    }
    out[used]=0;return text[*offset]!=0;
}
int terminal_pager_response(const char *text) {
    static const char *prefixes[]={"ARTICLE ","PREVIEW ","DRAFT LINES ","NOTES ","TREE ","UNREAD ","LIST ","SEARCH ","AUDIT ","FIND ","NEWS ","INFO ","PROFILE ","PROFILES ","FILES", "FILE ","FILEAUDIT","FILECATEGORIES","FILEBOARDS","FILENEWS","FILEHISTORY","FILETEXT","USERTABLE","USERLOG","BOARDLOG","BOARDACCESS"};
    for(size_t i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);i++)
        if(!strncmp(text,prefixes[i],strlen(prefixes[i]))) return 1;
    return !strcmp(text,"BOARDS\r\n") || !strncmp(text,"BOARDS\r\n",8) ||
           !strcmp(text,"MEMBERS\r\n") || !strncmp(text,"MEMBERS\r\n",9);
}
