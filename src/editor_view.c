#include "editor_view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
static int append(char *out,size_t capacity,size_t *used,const char *format,...) {
    va_list ap;va_start(ap,format);int n=vsnprintf(out+*used,capacity-*used,format,ap);va_end(ap);
    if(n<0 || (size_t)n>=capacity-*used) { out[0]=0;return 0; }
    *used+=(size_t)n;return 1;
}
static EditorViewResult listing(const EditorDocument *d,uint32_t first,uint32_t last,int numbers,int draft,char *out,size_t capacity) {
    if(!capacity)return EDITOR_VIEW_CAPACITY;
    out[0]=0;EditorInfo info;if(editor_inspect(d,&info)!=EDITOR_OK)return EDITOR_VIEW_INVALID;
    if(draft) { first=1;last=info.lines; }
    else if(!first || first>last || last>info.lines)return EDITOR_VIEW_INVALID;
    size_t used=0,cursor=0;EditorSpan span;unsigned n=0;
    if(draft && !append(out,capacity,&used,"DRAFT LINES %u\r\nTITLE %s\r\n",info.lines,d->title))return EDITOR_VIEW_CAPACITY;
    while(editor_next_line(d->body,info.bytes,&cursor,&span)==1) {
        n++;if(n<first || n>last)continue;
        if(draft) { if(!append(out,capacity,&used,"LINE %u ",n))return EDITOR_VIEW_CAPACITY; }
        else if(numbers && !append(out,capacity,&used,"%u ",n))return EDITOR_VIEW_CAPACITY;
        if(!append(out,capacity,&used,"%.*s\r\n",(int)span.size,span.text))return EDITOR_VIEW_CAPACITY;
    }
    if(draft && !append(out,capacity,&used,"END DRAFT\r\n"))return EDITOR_VIEW_CAPACITY;
    return EDITOR_VIEW_OK;
}
EditorViewResult editor_view_draft(const EditorDocument *d,char *out,size_t capacity) {
    return listing(d,0,0,1,1,out,capacity);
}
EditorViewResult editor_view_lines(const EditorDocument *d,uint32_t first,uint32_t last,int numbers,char *out,size_t capacity) {
    return listing(d,first,last,numbers,0,out,capacity);
}
EditorViewResult editor_view_search(const EditorDocument *d,uint32_t first,uint32_t last,const char *query,int numbers,int ansi,EditorViewWrite write,void *context,unsigned *matches) {
    *matches=0;EditorInfo info;
    if(!query || !*query || !write || editor_inspect(d,&info)!=EDITOR_OK || !first || first>last || last>info.lines)return EDITOR_VIEW_INVALID;
    size_t cursor=0;unsigned n=0;EditorSpan span;
    while(editor_next_line(d->body,info.bytes,&cursor,&span)==1) {
        n++;if(n<first || n>last)continue;
        size_t search=0;EditorMatch hit;if(editor_find_next(&span,query,&search,&hit)!=1)continue;
        size_t capacity=span.size*12+128;char *out=malloc(capacity);
        if(!out)return EDITOR_VIEW_MEMORY;
        size_t used=0,at=0;int ok=1;
        if(numbers)ok=append(out,capacity,&used,"%u ",n);
        do {
            if(ok)ok=append(out,capacity,&used,"%.*s%s%.*s%s",(int)(hit.offset-at),span.text+at,ansi?"\033[33m":"",(int)hit.size,span.text+hit.offset,ansi?"\033[0m":"");
            (*matches)++;at=hit.offset+hit.size;
        } while(editor_find_next(&span,query,&search,&hit)==1);
        if(ok)ok=append(out,capacity,&used,"%.*s\r\n",(int)(span.size-at),span.text+at);
        if(!ok) { free(out);return EDITOR_VIEW_CAPACITY; }
        ok=write(context,out);free(out);if(!ok)return EDITOR_VIEW_OUTPUT;
    }
    return EDITOR_VIEW_OK;
}
