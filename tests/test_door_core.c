#include "door_core.h"
#include "door_config.h"
#include "door_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static DoorCatalog catalog;
static unsigned checks;
static int paths(void *context,const DoorSpec *game) {
    assert(context==&checks);checks++;
    return strcmp(game->exe,"/missing") && strcmp(game->dir,"/missing");
}
static int add(const char *line) { return door_core_add(&catalog,line,paths,&checks); }
static void invalid(const char *line) {
    door_core_clear(&catalog);
    assert(add("9|0|10|/game|/work|Previously accepted"));
    assert(!add(line));assert(catalog.count==0);
    assert(!door_core_launch(&catalog,9,5,1,0));
}
static void configuration(void) {
    const char *bad[]={
        "1|0", "1|0|30|/game|/work|", "0|0|30|/game|/work|Title",
        "86401|0|30|/game|/work|Title", "1|6|30|/game|/work|Title",
        "1|0|0|/game|/work|Title", "1|0|86401|/game|/work|Title",
        "-1|0|30|/game|/work|Title", "+1|0|30|/game|/work|Title",
        " 1|0|30|/game|/work|Title", "1|0x0|30|/game|/work|Title",
        "1|0|999999999999999999999999|/game|/work|Title",
        "1|0|30|game|/work|Title", "1|0|30|/game|work|Title",
        "1|0|30|/missing|/work|Title", "1|0|30|/game|/missing|Title",
        "1|0|30|/game|/work|Line\nTitle", "1|0|30|/game|/work|Title\177",
        "1|0|30|/game|/work|Title|", "1|0|30|/game|/work|Title|unknown:1",
        "1|0|30|/game|/work|Title|none:0", "1|0|30|/game|/work|Title|dorinfo1:33",
        "1|0|30|/game|/work|Title|dorinfo1:01", "1|0|30|/game|/work|Title|none:1|extra",
        "1|0|30|/game|/work|Title|none:1junk", "1|0|30|/game|/work|Title|none:-1",
        "9|0|30|/game|/work|Duplicate ID"
    };
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)invalid(bad[i]);
    door_core_clear(&catalog);checks=0;
    assert(add(""));assert(add("# comment|with|bars"));assert(!checks);
    assert(add("86400|5|86400|/game|/work|日本語|dorinfo1:32"));
    assert(checks==1 && catalog.count==1 && catalog.games[0].slots==32 && catalog.games[0].drop==1);
    assert(add("01|00|0030|/game|/work|Legacy six fields"));
    assert(catalog.games[1].slots==0 && catalog.games[1].drop==0 && catalog.games[1].id==1);
    assert(add("2|1|10|/game|/work|Slots only|none:1"));
    assert(catalog.games[2].slots==1 && !catalog.games[2].drop);
    assert(!door_core_add(&catalog,"3|0|30|/game|/work|No validator",NULL,NULL));assert(!catalog.count);
    char line[DOOR_CONFIG_LINE_SIZE+1];memset(line,'x',sizeof(line)-1);line[sizeof(line)-1]=0;invalid(line);
    char title[162];memset(title,'a',160);title[160]=0;
    snprintf(line,sizeof(line),"1|0|30|/game|/work|%s",title);assert(add(line));
    title[160]='a';title[161]=0;snprintf(line,sizeof(line),"1|0|30|/game|/work|%s",title);invalid(line);
    for(unsigned i=1;i<=DOOR_CATALOG_LIMIT;i++) {
        snprintf(line,sizeof(line),"%u|0|30|/game|/work|Game",i);assert(add(line));
    }
    assert(catalog.count==DOOR_CATALOG_LIMIT);assert(add("# allowed after final game"));
    assert(!add("33|0|30|/game|/work|Overflow"));assert(!catalog.count);
}
static void selection(void) {
    assert(add("7|0|30|/private/executable|/private/work|Public"));
    assert(add("2|5|90|/private/executable|/private/work|Restricted"));
    DoorListing listing;door_core_list(&catalog,0,&listing);
    assert(listing.count==1 && listing.games[0].id==7);
    char output[8192];assert(door_protocol_list(&listing,output,sizeof(output)));
    assert(!strcmp(output,"GAMES\r\nGAME 7 30 Public\r\nEND GAMES\r\n"));
    assert(!strstr(output,"private") && !strstr(output,"Restricted"));
    assert(!door_core_launch(&catalog,7,5,0,0));
    assert(!door_core_launch(&catalog,7,5,1,1));
    assert(!door_core_launch(&catalog,2,4,1,0));
    assert(!door_core_launch(&catalog,3,5,1,0));
    const DoorSpec *game=door_core_launch(&catalog,2,5,1,0);
    assert(game && game->id==2 && game->seconds==90);
    door_core_list(&catalog,5,&listing);assert(listing.count==2 && listing.games[1].id==2);
    assert(door_protocol_list(&listing,output,sizeof(output)));
    size_t length=strlen(output);
    for(size_t size=0;size<=length;size++) {
        char guarded[8194];memset(guarded,'!',sizeof(guarded));
        assert(!door_protocol_list(&listing,guarded+1,size));
        assert(guarded[0]=='!' && guarded[size+1]=='!');if(size)assert(!guarded[1]);
    }
    assert(door_protocol_list(&listing,output,length+1));
    door_core_clear(&catalog);
    assert(listing.count==2 && !strcmp(listing.games[1].title,"Restricted")); /* owned snapshot */
    door_core_list(&catalog,5,&listing);assert(!listing.count);
    assert(door_protocol_list(&listing,output,sizeof(output)));
    assert(!strcmp(output,"GAMES\r\nEND GAMES\r\n"));
}
static void loading(void) {
    FILE *file=tmpfile();assert(file);
    fputs("# comment\r\n\r\n1|0|30|/game|/work|First\r\n2|5|60|/game|/work|Last",file);
    rewind(file);assert(door_config_read(&catalog,file,paths,&checks));assert(catalog.count==2);fclose(file);
    assert(door_config_load(&catalog,NULL,paths,&checks));assert(!catalog.count);
    file=tmpfile();assert(file);
    fputs("1|0|30|/game|/work|Valid\n2|0|30|/missing|/work|Invalid\n",file);
    rewind(file);assert(!door_config_read(&catalog,file,paths,&checks));assert(!catalog.count);fclose(file);
    file=tmpfile();assert(file);
    fputs("1|0|30|/game|/work|Valid\n#",file);
    for(unsigned i=0;i<DOOR_CONFIG_LINE_SIZE;i++)fputc('x',file);
    fputc('\n',file);rewind(file);assert(!door_config_read(&catalog,file,paths,&checks));assert(!catalog.count);fclose(file);
    assert(add("1|0|30|/game|/work|Valid"));
    assert(!door_config_read(&catalog,NULL,paths,&checks));assert(!catalog.count);
}
int main(void) {
    configuration();selection();loading();
    puts("PASS Door core: config validation, fail-closed load, filtered snapshots, launch admission, bounded protocol");
    return 0;
}
