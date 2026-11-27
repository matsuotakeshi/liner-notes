#define _POSIX_C_SOURCE 200809L
#include "big_model_input.h"
#include "chat_core.h"
#include "date_filter.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

BigModelCommand big_model_command(char *line) {
    char *argument=line+strcspn(line," \t");
    int key=argument==line+1?toupper((unsigned char)*line):0;
    return (BigModelCommand){key,argument+strspn(argument," \t")};
}

int big_model_date(const char *value,uint64_t *since) {
    if(strlen(value)==8) {
        char date[11];
        snprintf(date,sizeof(date),"%s%.8s",value[0]>='7'?"19":"20",value);
        return date_filter(date,since);
    }
    return date_filter(value,since);
}

int big_model_telegram_valid(const char *text) {
    if(!chat_core_text_valid(text))return 0;
    unsigned length=0;
    for(const unsigned char *p=(const unsigned char *)text;*p;p++)
        if((*p&0xc0)!=0x80 && ++length>21)return 0;
    return 1;
}

int big_model_format_header(char *out,size_t capacity,uint32_t id,uint64_t timestamp,
                            const char *author,const char *title) {
    char date[16]="--/--/--",clock[16]="--:--:--";
    time_t when=(time_t)timestamp;
    struct tm civil;
    if(timestamp && when>=0 && (uint64_t)when==timestamp) {
#ifdef _WIN32
        int valid=localtime_s(&civil,&when)==0;
#else
        int valid=localtime_r(&when,&civil)!=NULL;
#endif
        if(valid) {
            strftime(date,sizeof(date),"%y-%m-%d",&civil);
            strftime(clock,sizeof(clock),"%H:%M:%S",&civil);
        }
    }
    int length=snprintf(out,capacity,"%05u  %s %s  %s  %s\r\n",id,date,clock,author,title);
    return length>=0 && (size_t)length<capacity;
}
