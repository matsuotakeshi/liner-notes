/* Internal offline importer. Input is a validated newline-separated stream list.
 * The caller supplies a NEW, empty destination directory, never the live DB. */
#define _XOPEN_SOURCE 700
#include "storage_sqlite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>

int main(int argc,char **argv) {
    if(argc!=3) { fprintf(stderr,"usage: dbconvert NEW_DATABASE SOURCE_DIRECTORY < stream-list\n");return 2; }
    struct stat st;
    if(!lstat(argv[1],&st)) { fprintf(stderr,"Database already exists\n");return 1; }
    char *source=realpath(argv[2],NULL),*output=strdup(argv[1]);
    if(!source || !output)return 1;
    char *slash=strrchr(output,'/');
    if(!slash || !slash[1])return 1;
    *slash=0;char *name=slash+1;
    if(chdir(output) || !storage_init_structured(name))return 1;
    char path[PATH_MAX],input[PATH_MAX];unsigned char data[65536];int ok=1;
    while(ok && fgets(path,sizeof(path),stdin)) {
        size_t n=strlen(path);
        if(!n || path[n-1]!='\n') { ok=0;break; }path[--n]=0;
        if(!n || path[0]=='/' || strchr(path,'\r') || strstr(path,"..") ||
           snprintf(input,sizeof(input),"%s/%s",source,path)>=(int)sizeof(input)) { ok=0;break; }
        /* No original files may exist at a virtual stream destination. */
        if(!lstat(path,&st)) { ok=0;break; }
        int src=open(input,O_RDONLY|O_NOFOLLOW);
        if(src<0 || fstat(src,&st) || !S_ISREG(st.st_mode) || st.st_nlink!=1) { if(src>=0)close(src);ok=0;break; }
        int dst=storage_open(path,O_RDWR|O_CREAT|O_EXCL,0600);ssize_t count=0;
        ok=dst>=0;
        while(ok && (count=read(src,data,sizeof(data)))>0)ok=storage_write(dst,data,(size_t)count)==count;
        if(count<0)ok=0;
        if(ok)ok=storage_sync(dst)==0;
        close(src);if(dst>=0)storage_close(dst);
    }
    if(ferror(stdin))ok=0;
    storage_shutdown();free(source);free(output);
    if(!ok)fprintf(stderr,"Structured import failed; discard this incomplete copy\n");
    return ok?0:1;
}
