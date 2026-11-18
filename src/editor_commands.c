#include "editor_commands.h"
#include "store_limits.h"
#include <string.h>
static int editor_number(const char *p,uint32_t *number,const char **rest) {
    if(*p<'0' || *p>'9') return 0;
    uint32_t n=0;
    while(*p>='0' && *p<='9') { unsigned digit=(unsigned)(*p++-'0');if(n>(CONTENT_ID_MAX+1-digit)/10)return 0;n=n*10+digit; }
    if(!n) return 0;
    *number=n; *rest=p; return 1;
}

int editor_command_word(const char *line,const char *word) {
    size_t n=strlen(word);return !strncmp(line,word,n) && (!line[n] || line[n]==' ');
}
DraftCommand editor_draft_command(const char *line) {
    DraftCommand q={0};
    if(!strcmp(line,"/UNDO")) { q.kind=DRAFT_MUTATION;q.operation=EDITOR_UNDO;return q; }
    if(!strcmp(line,"/LINES")) { q.kind=DRAFT_LINES;return q; }
    if(editor_command_word(line,"/TITLE")) {
        q.kind=DRAFT_MUTATION;q.operation=EDITOR_SET_TITLE;q.text=line[6]?line+7:"";return q;
    }
    int replace=editor_command_word(line,"/REPLACE"),remove=editor_command_word(line,"/DELETE"),insert=editor_command_word(line,"/INSERT");
    if(!replace && !remove && !insert)return q;
    size_t prefix=replace?8:7;uint32_t number;const char *tail;
    q.kind=DRAFT_INVALID;
    if(!line[prefix] || !editor_number(line+prefix+1,&number,&tail) || (remove && *tail) || (!remove && *tail && *tail!=' '))return q;
    q.kind=DRAFT_MUTATION;q.operation=insert?EDITOR_INSERT:replace?EDITOR_REPLACE:EDITOR_DELETE;
    q.line=number;q.text=!remove && *tail?tail+1:"";return q;
}
LineCommand editor_line_command(const char *line,uint32_t count,uint32_t current) {
    LineCommand q={0};const char *tail;
    if(!current || current>count+1)current=1;
    if(*line>='0' && *line<='9') {
        if(editor_number(line,&q.first,&tail) && !*tail && q.first<=count)q.kind=LINE_SELECT;
        return q;
    }
    char key=*line;if(key>='a' && key<='z')key-=32;
    if(key=='N' && !line[1]) { q.kind=LINE_NEW;return q; }
    if(key=='U' && !line[1]) { q.kind=LINE_UPLOAD;return q; }
    if(key=='A' && !line[1]) { q.kind=LINE_APPEND;return q; }
    if(key=='S') {
        q.first=1;q.last=count;const char *query=line+1;
        if(*query && *query!=' ') {
            if(!editor_number(query,&q.first,&query))return q;
            q.last=q.first;
            if(*query=='-') { query++;q.last=count;if(*query && *query!=' ' && !editor_number(query,&q.last,&query))return q; }
            if(*query && *query!=' ')return q;
        }
        if(!q.first || q.first>q.last || q.last>count)return q;
        while(*query==' ')query++;
        q.kind=LINE_SEARCH;q.query=query;return q;
    }
    if(!key || !strchr("LEIDQ",key)) { q.kind=LINE_HELP;return q; }
    q.first=key=='L'?1:current;q.last=key=='L'?count:q.first;tail=line+1;
    if(*tail) {
        if(*tail!='-') { if(!editor_number(tail,&q.first,&tail))return q;q.last=q.first; }
        if(*tail=='-') { tail++;q.last=count;if(*tail && !editor_number(tail,&q.last,&tail))return q; }
        if(*tail)return q;
    }
    if(!(key=='L' && !count) && (!q.first || q.first>q.last || q.last>count+(key=='I'?1u:0u)))return q;
    q.kind=key=='L'?LINE_LIST:key=='E'?LINE_EDIT:key=='I'?LINE_INSERT:key=='D'?LINE_DELETE:LINE_QUOTE;return q;
}
