/* Generates only test copies from one authenticated account record. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static void put(unsigned char *p,uint32_t n) { for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(8*i)); }
int main(int argc,char **argv) {
    if(argc!=4)return 2;
    unsigned count=(unsigned)strtoul(argv[3],NULL,10);if(!count || count>999999)return 2;
    FILE *in=fopen(argv[1],"rb");if(!in)return 1;unsigned char header[8],row[128];
    if(fread(header,1,8,in)!=8 || fread(row,1,128,in)!=128 || row[0]!='A')return 1;
    fclose(in);FILE *out=fopen(argv[2],"wb");if(!out)return 1;
    if(fwrite(header,1,8,out)!=8)return 1;
    for(unsigned id=1;id<=count;id++) {
        put(row+4,id);if(id>1) { memset(row+8,0,32);snprintf((char *)row+8,32,"u%06u",id); }
        uint32_t h=2166136261u;for(unsigned i=0;i<124;i++)h=(h^row[i])*16777619u;put(row+124,h);
        if(fwrite(row,1,128,out)!=128)return 1;
    }
    return fclose(out)!=0;
}
