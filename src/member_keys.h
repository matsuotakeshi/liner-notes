#ifndef MEMBER_KEYS_H
#define MEMBER_KEYS_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif
/* Bounded, fail-closed administrator file: numeric member ID + SHA256 hex.
 * Header-only so existing memberctl/test consumers need no SSH dependency. */
static int member_key_allowed(const char *path,uint32_t id,const char *hash) {
#ifdef _WIN32
    (void)path;(void)id;(void)hash;return 0;
#else
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return 0;
    struct stat st;
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size>1024*1024 ||
       (st.st_mode&022) || (st.st_uid!=0 && st.st_uid!=geteuid())) { close(fd);return 0; }
    FILE *f=fdopen(fd,"r");if(!f) { close(fd);return 0; }
    char line[256];int allowed=0,valid=1;
    while(fgets(line,sizeof(line),f)) {
        if(!strchr(line,'\n') && !feof(f)) { valid=0;break; }
        char *p=line;while(*p==' ' || *p=='\t')p++;
        if(!*p || *p=='#' || *p=='\n' || *p=='\r')continue;
        if(*p<'1' || *p>'9') { valid=0;break; }
        char *end;unsigned long n=strtoul(p,&end,10);
        if(n>UINT32_MAX || (*end!=' ' && *end!='\t')) { valid=0;break; }
        p=end;while(*p==' ' || *p=='\t')p++;
        size_t len=strcspn(p," \t\r\n");
        if(len!=64 || strspn(p,"0123456789abcdef")!=64) { valid=0;break; }
        end=p+len;while(*end==' ' || *end=='\t' || *end=='\r' || *end=='\n')end++;
        if(*end && *end!='#') { valid=0;break; }
        if(n==id && !memcmp(p,hash,64))allowed=1;
    }
    if(ferror(f))valid=0;
    fclose(f);return valid && allowed;
#endif
}
#endif
