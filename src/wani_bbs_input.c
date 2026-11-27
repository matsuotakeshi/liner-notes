#include "wani_bbs_input.h"
#include <string.h>

static unsigned char fold(unsigned char c) {
    return c>='A' && c<='Z'?(unsigned char)(c-'A'+'a'):c;
}

WaniCommand wani_command(char *line) {
    line+=strspn(line," \t");
    char *end=line+strcspn(line," \t");
    if(*end)*end++=0;
    return (WaniCommand){line,end+strspn(end," \t")};
}

int wani_equal(const char *a,const char *b) {
    while(*a && fold((unsigned char)*a)==fold((unsigned char)*b)) { a++;b++; }
    return !*a && !*b;
}

int wani_match(const char *pattern,const char *name) {
    const char *retry=NULL,*start=NULL;
    while(*name) {
        if(*pattern=='*') { retry=++pattern;start=name; }
        else if(*pattern=='?' || fold((unsigned char)*pattern)==fold((unsigned char)*name)) {
            pattern++;name++;
        } else if(retry) { pattern=retry;name=++start; }
        else return 0;
    }
    while(*pattern=='*')pattern++;
    return !*pattern;
}

static int endpoint(const char **text,uint32_t current,uint32_t last,uint32_t *value) {
    const char *s=*text;
    if(*s=='=' || *s=='$') {
        *value=*s=='='?current:last;
        *text=s+1;
        return *value!=0;
    }
    uint32_t n=0;
    if(*s<'0' || *s>'9')return 0;
    do {
        unsigned digit=(unsigned)(*s-'0');
        if(n>(UINT32_MAX-digit)/10)return 0;
        n=n*10+digit;
        s++;
    } while(*s>='0' && *s<='9');
    *text=s;*value=n;
    return n!=0;
}

int wani_range(const char *text,uint32_t current,uint32_t last,uint32_t *first,uint32_t *end) {
    uint32_t lo=1,hi=last;
    if(*text!=',' && !endpoint(&text,current,last,&lo))return 0;
    if(*text==',') { text++;if(*text && !endpoint(&text,current,last,&hi))return 0; }
    else hi=lo;
    if(*text || !hi)return 0;
    *first=lo;*end=hi;
    return 1;
}
