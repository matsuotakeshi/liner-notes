#include "members.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
int main(int argc,char **argv) {
    struct stat st;
    if(argc!=4 || strcmp(argv[1],"compact")) { fprintf(stderr,"usage: memberctl compact INPUT NEW_OUTPUT (offline)\n");return 2; }
    if(stat(argv[2],&st) || !members_open(argv[2])) { fprintf(stderr,"memberctl: cannot open/lock/validate source\n");members_close();return 1; }
    int ok=members_compact(argv[3]);members_close();
    if(!ok) { fprintf(stderr,"memberctl: compaction failed; input preserved\n");return 1; }
    puts("MEMBERS COMPACTED");return 0;
}
