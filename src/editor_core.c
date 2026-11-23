#include "editor_core.h"
#include <stdlib.h>
#include <string.h>
#include <openssl/crypto.h>

static int storage(const EditorDocument *d) {
    return d && d->title && d->body && d->undo_title && d->undo_body && d->undo_available;
}
static int text_valid(const char *text) {
    if(!text)return 0;
    for(const unsigned char *p=(const unsigned char *)text;*p;p++)if(*p<32 || *p==127)return 0;
    return 1;
}
int editor_next_line(const char *body,size_t bytes,size_t *cursor,EditorSpan *out) {
    if(!out)return -1;
    memset(out,0,sizeof(*out));
    if(!body || !cursor || bytes>CONTENT_BODY || *cursor>bytes)return -1;
    if(*cursor==bytes)return 0;
    size_t at=*cursor;
    for(size_t i=at;i<bytes;i++)if(body[i]=='\r' || body[i]=='\n') {
        if(body[i]!='\r' || i+1>=bytes || body[i+1]!='\n')return -1;
        *out=(EditorSpan){body+at,i-at,at};*cursor=i+2;return 1;
    }
    return -1;
}
EditorOutcome editor_inspect(const EditorDocument *d,EditorInfo *out) {
    if(!out)return EDITOR_INVALID;
    memset(out,0,sizeof(*out));if(!storage(d))return EDITOR_INVALID;
    const char *end=memchr(d->body,0,CONTENT_BODY+1);
    if(!end || !memchr(d->title,0,EDITOR_TITLE+1))return EDITOR_INVALID;
    size_t bytes=(size_t)(end-d->body),at=0;uint32_t lines=0;EditorSpan span;int result;
    while((result=editor_next_line(d->body,bytes,&at,&span))==1)lines++;
    if(result<0)return EDITOR_INVALID;
    *out=(EditorInfo){bytes,lines};return EDITOR_OK;
}
EditorOutcome editor_range(const EditorDocument *d,uint32_t first,uint32_t last,EditorSpan *out) {
    if(!out)return EDITOR_INVALID;
    memset(out,0,sizeof(*out));EditorInfo info;EditorOutcome result=editor_inspect(d,&info);
    if(result!=EDITOR_OK)return result;
    if(!first || first>last || last>info.lines)return EDITOR_LINE;
    size_t at=0,begin=0;EditorSpan line;
    for(uint32_t n=1;n<=last;n++) { size_t before=at;editor_next_line(d->body,info.bytes,&at,&line);if(n==first)begin=before; }
    *out=(EditorSpan){d->body+begin,at-begin,begin};return EDITOR_OK;
}
int editor_find_next(const EditorSpan *line,const char *query,size_t *cursor,EditorMatch *out) {
    if(!out)return -1;
    memset(out,0,sizeof(*out));
    if(!line || !line->text || !query || !*query || !cursor || *cursor>line->size)return -1;
    size_t n=strlen(query);
    if(n<=line->size)for(size_t i=*cursor;i<=line->size-n;i++)if(!memcmp(line->text+i,query,n)) {
        *out=(EditorMatch){i,n};*cursor=i+n;return 1;
    }
    *cursor=line->size;return 0;
}
void editor_discard_undo(EditorDocument *d) {
    if(!storage(d))return;
    *d->undo_available=0;OPENSSL_cleanse(d->undo_title,EDITOR_TITLE+1);OPENSSL_cleanse(d->undo_body,CONTENT_BODY+1);
}
static void save(EditorDocument *d) {
    memcpy(d->undo_title,d->title,EDITOR_TITLE+1);memcpy(d->undo_body,d->body,CONTENT_BODY+1);*d->undo_available=1;
}
static void clear_tail(EditorDocument *d,size_t bytes) { OPENSSL_cleanse(d->body+bytes,CONTENT_BODY+1-bytes); }
EditorOutcome editor_apply(EditorDocument *d,EditorOperation op,uint32_t first,uint32_t last,const char *text) {
    if(!storage(d))return EDITOR_INVALID;
    if(op==EDITOR_UNDO) {
        if(!*d->undo_available)return EDITOR_NO_UNDO;
        EditorDocument prior=*d;prior.title=d->undo_title;prior.body=d->undo_body;EditorInfo check;
        if(editor_inspect(&prior,&check)!=EDITOR_OK)return EDITOR_INVALID;
        memcpy(d->title,d->undo_title,EDITOR_TITLE+1);memcpy(d->body,d->undo_body,CONTENT_BODY+1);editor_discard_undo(d);return EDITOR_OK;
    }
    EditorInfo info;EditorOutcome result=editor_inspect(d,&info);if(result!=EDITOR_OK)return result;
    if(op==EDITOR_SET_TITLE) {
        if(!text || !*text || strlen(text)>EDITOR_TITLE || !text_valid(text))return EDITOR_TITLE_INVALID;
        if(strcmp(d->title,text)) { save(d);memcpy(d->title,text,strlen(text)+1); }
        return EDITOR_OK;
    }
    if(op==EDITOR_CLEAR) { save(d);clear_tail(d,0);return EDITOR_OK; }
    if(op==EDITOR_APPEND_BLOCK) {
        if(!text)return EDITOR_INVALID;
        size_t n=strlen(text);if(n>CONTENT_BODY-info.bytes)return EDITOR_LIMIT;
        size_t at=0;EditorSpan span;int valid;
        while((valid=editor_next_line(text,n,&at,&span))==1) {}
        if(valid<0)return EDITOR_INVALID;
        save(d);memcpy(d->body+info.bytes,text,n+1);clear_tail(d,info.bytes+n);return EDITOR_OK;
    }
    if(op==EDITOR_APPEND_LINE) { first=info.lines+1;op=EDITOR_INSERT; }
    EditorSpan range={0};size_t begin=info.bytes,old=0;
    if(op==EDITOR_INSERT) {
        if(!first || first>info.lines+1)return EDITOR_LINE;
        if(first<=info.lines) { editor_range(d,first,first,&range);begin=range.offset; }
    } else if(op==EDITOR_REPLACE || op==EDITOR_DELETE || op==EDITOR_QUOTE) {
        if(op==EDITOR_REPLACE)last=first;
        result=editor_range(d,first,last,&range);if(result!=EDITOR_OK)return result;
        begin=range.offset;old=range.size;
    } else return EDITOR_INVALID;
    if(op==EDITOR_QUOTE) {
        size_t extra=2*(size_t)(last-first+1);if(extra>CONTENT_BODY-info.bytes)return EDITOR_LIMIT;
        save(d);size_t at=begin,used=begin;EditorSpan line;
        while(at<begin+old) {
            editor_next_line(d->undo_body,info.bytes,&at,&line);
            memcpy(d->body+used,"> ",2);used+=2;memcpy(d->body+used,line.text,line.size+2);used+=line.size+2;
        }
        memcpy(d->body+used,d->undo_body+begin+old,info.bytes-begin-old+1);clear_tail(d,info.bytes+extra);return EDITOR_OK;
    }
    size_t n=0,add=0;
    if(op!=EDITOR_DELETE) {
        if(!text_valid(text))return EDITOR_CONTROL;
        n=strlen(text);if(n>CONTENT_BODY-2)return EDITOR_LIMIT;add=n+2;
    }
    if(add>CONTENT_BODY-(info.bytes-old))return EDITOR_LIMIT;
    if(op==EDITOR_REPLACE && old==add && !memcmp(d->body+begin,text,n))return EDITOR_OK;
    save(d);memmove(d->body+begin+add,d->body+begin+old,info.bytes-begin-old+1);
    if(op!=EDITOR_DELETE) { memcpy(d->body+begin,text,n);memcpy(d->body+begin+n,"\r\n",2); }
    clear_tail(d,info.bytes-old+add);return EDITOR_OK;
}

static int append_text(char *out,size_t *used,const char *text,size_t size) {
    if(size>CONTENT_BODY-*used)return 0;
    memcpy(out+*used,text,size);
    *used+=size;
    return 1;
}

EditorOutcome editor_transform(EditorDocument *d,uint32_t first,uint32_t last,
                               int complement,const char *needle,const char *replacement) {
    EditorInfo info;
    EditorOutcome result=editor_inspect(d,&info);
    if(result!=EDITOR_OK)return result;
    if(!first || first>last || last>info.lines)return EDITOR_LINE;
    if(needle && (!*needle || !text_valid(needle) || !text_valid(replacement)))
        return EDITOR_CONTROL;
    char *staged=malloc(CONTENT_BODY+1);
    if(!staged)return EDITOR_LIMIT;
    EditorSpan line;
    size_t cursor=0,used=0;
    for(uint32_t n=1;n<=info.lines;n++) {
        editor_next_line(d->body,info.bytes,&cursor,&line);
        int selected=(n>=first && n<=last)!=!!complement;
        if(selected && !needle)continue;
        size_t start=0,search=0;
        EditorMatch match;
        while(selected && editor_find_next(&line,needle,&search,&match)>0) {
            if(!append_text(staged,&used,line.text+start,match.offset-start) ||
               !append_text(staged,&used,replacement,strlen(replacement))) {
                result=EDITOR_LIMIT;
                goto done;
            }
            start=match.offset+match.size;
        }
        if(!append_text(staged,&used,line.text+start,line.size+2-start)) {
            result=EDITOR_LIMIT;
            goto done;
        }
    }
    if(used!=info.bytes || memcmp(staged,d->body,used)) {
        save(d);
        memcpy(d->body,staged,used);
        clear_tail(d,used);
    }
done:
    OPENSSL_cleanse(staged,used);
    free(staged);
    return result;
}
