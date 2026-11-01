/* Streaming test data generator: valid persisted bodies, no runtime internals. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static void put(unsigned char *p,uint32_t n) { for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(8*i)); }
static int record(FILE *f,unsigned char *row,size_t n) { uint32_t h=2166136261u;for(size_t i=0;i<n-4;i++)h=(h^row[i])*16777619u;put(row+n-4,h);return fwrite(row,1,n,f)==n; }
int main(int argc,char **argv) {
    if(argc!=4)return 2;
    unsigned count=(unsigned)strtoul(argv[3],NULL,10);if(!count || count>100000)return 2;
    FILE *board=fopen(argv[1],"wb"),*mail=fopen(argv[2],"wb");if(!board || !mail)return 1;
    if(fwrite("RTBOARD4",1,8,board)!=8 || fwrite("LNMAIL01",1,8,mail)!=8)return 1;
    enum { BODY=4096 };unsigned char row[BODY+144];
    for(unsigned id=1;id<=count;id++) {
        memset(row,0,sizeof(row));put(row,id|0x80000000u);put(row+8,6);put(row+12,BODY);memcpy(row+16,"scaled",6);memset(row+96,'x',BODY);memcpy(row+96,"scaled-body",11);row[96+BODY-2]='\r';row[96+BODY-1]='\n';memcpy(row+BODY+104,"alice",5);put(row+BODY+136,1);
        if(!record(board,row,BODY+144))return 1;
        memset(row,0,sizeof(row));row[0]='s';put(row+4,id);put(row+8,id);put(row+12,1);put(row+16,1);put(row+20,2);put(row+28,6);put(row+32,BODY);memcpy(row+44,"scaled",6);memset(row+124,'y',BODY);memcpy(row+124,"scaled-mail",11);row[124+BODY-2]='\r';row[124+BODY-1]='\n';
        if(!record(mail,row,BODY+128))return 1;
    }
    int a=fclose(board),b=fclose(mail);return a || b;
}
