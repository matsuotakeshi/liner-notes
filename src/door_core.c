#include "door_core.h"
#include <stdlib.h>
#include <string.h>

void door_core_clear(DoorCatalog *catalog) { memset(catalog,0,sizeof(*catalog)); }
static int invalid(DoorCatalog *catalog) { door_core_clear(catalog);return 0; }

int door_core_add(DoorCatalog *catalog,const char *source,DoorPathCheck check,void *context) {
    char line[DOOR_CONFIG_LINE_SIZE];DoorSpec game={0};
    if(!source || strlen(source)>=sizeof(line))return invalid(catalog);
    strcpy(line,source);
    if(!*line || *line=='#')return 1;
    if(catalog->count==DOOR_CATALOG_LIMIT)return invalid(catalog);
    char *options=NULL,*parts[6],*p=line;unsigned i;
    for(i=0;i<6;i++) {
        parts[i]=p;char *bar=strchr(p,'|');
        if(i<5) { if(!bar)break;*bar=0;p=bar+1; }
        else if(bar) { *bar=0;options=bar+1; }
    }
    if(i!=6)return invalid(catalog);
    unsigned *numbers[]={&game.id,&game.level,&game.seconds};
    for(i=0;i<3;i++) {
        char *end;unsigned long value=strtoul(parts[i],&end,10);
        if(!*parts[i] || *end || *parts[i]<'0' || *parts[i]>'9' || value>86400)break;
        *numbers[i]=(unsigned)value;
    }
    if(i!=3 || !game.id || game.level>5 || !game.seconds || parts[3][0]!='/' || parts[4][0]!='/'
        || strlen(parts[3])>=sizeof(game.exe) || strlen(parts[4])>=sizeof(game.dir)
        || !*parts[5] || strlen(parts[5])>=sizeof(game.title))return invalid(catalog);
    if(options) {
        const char *value=NULL;
        if(!strncmp(options,"dorinfo1:",9)) { game.drop=1;value=options+9; }
        else if(!strncmp(options,"none:",5))value=options+5;
        char *end=NULL;unsigned long slots=value?strtoul(value,&end,10):0;
        if(!value || *value<'1' || *value>'9' || *end || slots<1 || slots>32)return invalid(catalog);
        game.slots=(unsigned)slots;
    }
    for(const unsigned char *q=(const unsigned char *)parts[5];*q;q++)
        if(*q<32 || *q==127)return invalid(catalog);
    for(i=0;i<catalog->count;i++)if(catalog->games[i].id==game.id)return invalid(catalog);
    strcpy(game.exe,parts[3]);strcpy(game.dir,parts[4]);strcpy(game.title,parts[5]);
    if(!check || !check(context,&game))return invalid(catalog);
    catalog->games[catalog->count++]=game;
    return 1;
}
void door_core_list(const DoorCatalog *catalog,unsigned level,DoorListing *listing) {
    memset(listing,0,sizeof(*listing));
    for(unsigned i=0;i<catalog->count;i++)if(level>=catalog->games[i].level) {
        const DoorSpec *game=&catalog->games[i];DoorSummary *item=&listing->games[listing->count++];
        item->id=game->id;item->seconds=game->seconds;strcpy(item->title,game->title);
    }
}
const DoorSpec *door_core_launch(const DoorCatalog *catalog,unsigned id,unsigned level,uint32_t user,int is_root) {
    if(!user || is_root)return NULL;
    for(unsigned i=0;i<catalog->count;i++)
        if(catalog->games[i].id==id && level>=catalog->games[i].level)return &catalog->games[i];
    return NULL;
}
