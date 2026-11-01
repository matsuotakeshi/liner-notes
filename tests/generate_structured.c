/* Streaming test data generator: valid persisted bodies, no runtime internals. */
#include <stdio.h>
#include "storage_sqlite.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static void put(unsigned char *p,uint32_t n) { for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(8*i)); }
static int record(int f,unsigned char *row,size_t n) { uint32_t h=2166136261u;for(size_t i=0;i<n-4;i++)h=(h^row[i])*16777619u;put(row+n-4,h);return storage_write(f,row,n)==(ssize_t)n; }
int main(int argc,char **argv) {
    if(argc!=5)return 2;
    unsigned count=(unsigned)strtoul(argv[4],NULL,10);if(!count || count>100000)return 2;
    if(!storage_init_structured(argv[1]))return 1;int board=storage_open(argv[2],O_RDWR,0600),mail=storage_open(argv[3],O_RDWR,0600);if(board<0 || mail<0)return 1;
    if(storage_write(board,"RTBOARD4",8)!=8 || storage_sync(board))return 1;
    enum { BODY=4096 };unsigned char row[BODY+144];
    for(unsigned id=1;id<=count;id++) {
        memset(row,0,sizeof(row));put(row,id|0x80000000u);put(row+8,6);put(row+12,BODY);memcpy(row+16,"scaled",6);memset(row+96,'x',BODY);memcpy(row+96,"scaled-body",11);row[96+BODY-2]='\r';row[96+BODY-1]='\n';memcpy(row+BODY+104,"alice",5);put(row+BODY+136,1);
        if(!record(board,row,BODY+144))return 1;
    }
    if(storage_sync(board) || storage_close(board))return 1;
    if(storage_write(mail,"LNMAIL01",8)!=8 || storage_sync(mail))return 1;
    for(unsigned id=1;id<=count;id++) {
        memset(row,0,sizeof(row));row[0]='s';put(row+4,id);put(row+8,id);put(row+12,1);put(row+16,1);put(row+20,2);put(row+28,6);put(row+32,BODY);memcpy(row+44,"scaled",6);memset(row+124,'y',BODY);memcpy(row+124,"scaled-mail",11);row[124+BODY-2]='\r';row[124+BODY-1]='\n';
        if(!record(mail,row,BODY+128))return 1;
    }
    int a=storage_sync(mail),b=storage_close(mail);storage_shutdown();return a || b;
}
