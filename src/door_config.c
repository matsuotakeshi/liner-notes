#include "door_config.h"
#include <stdio.h>
#include <string.h>

int door_config_read(DoorCatalog *catalog,FILE *file,DoorPathCheck check,void *context) {
    door_core_clear(catalog);
    if(!file)return 0;
    char line[DOOR_CONFIG_LINE_SIZE];int ok=1;
    while(fgets(line,sizeof(line),file)) {
        size_t n=strlen(line);
        if(n && line[n-1]=='\n')line[--n]=0;
        else if(!feof(file)) { ok=0;break; }
        if(n && line[n-1]=='\r')line[--n]=0;
        if(!door_core_add(catalog,line,check,context)) { ok=0;break; }
    }
    if(ferror(file))ok=0;
    if(!ok)door_core_clear(catalog);
    return ok;
}

int door_config_load(DoorCatalog *catalog,const char *path,DoorPathCheck check,void *context) {
    door_core_clear(catalog);if(!path)return 1;
    FILE *file=fopen(path,"r");if(!file)return 0;
    int ok=door_config_read(catalog,file,check,context);fclose(file);return ok;
}
