#include "menu.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

static char menus[LANGUAGE_COUNT][8192];
static char sections[LANGUAGE_COUNT][6][8192];
const char *menu_section_text(Language language,unsigned section) {
    return language>=0 && language<LANGUAGE_COUNT && section<6 && sections[language][section][0]?sections[language][section]:NULL;
}
const char *menu_text(Language language) {
    return language>=0 && language<LANGUAGE_COUNT && menus[language][0]?menus[language]:NULL;
}
static int normalize(const unsigned char *input,size_t size,char *out,size_t maximum) {
    size_t i=0,n=0;
    if(size>=3 && !memcmp(input,"\xef\xbb\xbf",3)) i=3;
    while(i<size) {
        unsigned c=input[i],length=1,cp=c;
        if(c=='\r' || c=='\n') {
            if(n+2>=maximum) return 0;
            out[n++]='\r'; out[n++]='\n'; i++;
            if(c=='\r' && i<size && input[i]=='\n') i++;
            continue;
        }
        if(c>=128) {
            length=c>=0xc2 && c<=0xdf?2:c>=0xe0 && c<=0xef?3:c>=0xf0 && c<=0xf4?4:0;
            if(!length || i+length>size) return 0;
            cp=c&((1u<<(7-length))-1);
            for(unsigned j=1;j<length;j++) {
                if((input[i+j]&0xc0)!=0x80) return 0;
                cp=(cp<<6)|(input[i+j]&63);
            }
            if((length==2 && cp<128) || (length==3 && cp<2048) || (length==4 && cp<65536) ||
               cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return 0;
        }
        if((cp<32 && cp!='\t') || (cp>=127 && cp<=159) || n+length>=maximum) return 0;
        memcpy(out+n,input+i,length); n+=length; i+=length;
    }
    if(!n) return 0;
    if(out[n-1]!='\n') {
        if(n+2>=maximum) return 0;
        out[n++]='\r'; out[n++]='\n';
    }
    out[n]=0; return 1;
}
int menu_load(const char *directory,int required) {
    memset(menus,0,sizeof(menus));memset(sections,0,sizeof(sections));
    for(int language=0;language<LANGUAGE_COUNT;language++) {
        char path[4096]; int n=snprintf(path,sizeof(path),"%s/%s.txt",directory,language_code((Language)language));
        if(n<0 || (size_t)n>=sizeof(path)) return 0;
        FILE *f=fopen(path,"rb");
        if(!f) {
            if(!required && errno==ENOENT) continue;
            fprintf(stderr,"menus: cannot read %s\n",path); return 0;
        }
        unsigned char input[4097]; size_t size=fread(input,1,sizeof(input),f);
        int bad=ferror(f); if(fclose(f)) bad=1;
        if(bad || size>4096 || !normalize(input,size,menus[language],sizeof(menus[language]))) {
            fprintf(stderr,"menus: invalid UTF-8, control character, empty or oversized file: %s\n",path); return 0;
        }
    }
    const char *names[]={"board","reader","settings","profile","admin","sigop"};
    for(int language=0;language<LANGUAGE_COUNT;language++)for(unsigned part=0;part<6;part++) {
        char path[4096];int n=snprintf(path,sizeof(path),"%s/%s-%s.txt",directory,names[part],language_code((Language)language));
        if(n<0 || (size_t)n>=sizeof(path))return 0;
        FILE *f=fopen(path,"rb");if(!f) { if(errno==ENOENT)continue;return 0; }
        unsigned char input[4097];size_t size=fread(input,1,sizeof(input),f);int bad=ferror(f);if(fclose(f))bad=1;
        if(bad || size>4096 || !normalize(input,size,sections[language][part],sizeof(sections[language][part])))return 0;
    }
    return 1;
}

static char banner[8192];
const char *banner_text(void) { return banner; }
int banner_load(const char *path) {
    banner[0]=0;FILE *f=fopen(path,"rb");
    if(!f)return errno==ENOENT;
    unsigned char input[4097];size_t n=fread(input,1,sizeof(input),f);int bad=ferror(f);fclose(f);
    if(bad || n>4096)return 0;
    if(!n || (n==3 && !memcmp(input,"\xef\xbb\xbf",3)))return 1;
    return normalize(input,n,banner,sizeof(banner));
}

static const char *help_topics[]={"INDEX","READ","UNREAD","POST","DRAFT","SETTINGS","ACCOUNT","ADMIN","CONTENT","PROFILE","FILES","CHAT","MAIL","GATHER","SIGOP","EDITOR","ADMINMEMBERS","ADMINBOARDS","ADMINFILES","ADMINCONNECTIONS","ADMINSTORAGE"};
static char helps[LANGUAGE_COUNT][21][32768];
const char *help_text(Language language,const char *topic) {
    if(language<0 || language>=LANGUAGE_COUNT)return NULL;
    if(!*topic)topic="INDEX";
    if(!strcmp(topic,"TALK"))topic="CHAT";
    for(unsigned i=0;i<21;i++)if(!strcmp(topic,help_topics[i]))return helps[language][i][0]?helps[language][i]:NULL;
    return NULL;
}
int help_load(const char *directory,int required) {
    memset(helps,0,sizeof(helps));
    for(int l=0;l<LANGUAGE_COUNT;l++)for(unsigned i=0;i<21;i++) {
        char path[4096],topic[24];snprintf(topic,sizeof(topic),"%s",help_topics[i]);for(char *p=topic;*p;p++)if(*p>='A' && *p<='Z')*p+=32;
        int n=snprintf(path,sizeof(path),"%s/%s/%s.txt",directory,language_code((Language)l),topic);if(n<=0 || (size_t)n>=sizeof(path))return 0;
        FILE *file=fopen(path,"rb");if(!file) { if(!required && errno==ENOENT)continue;fprintf(stderr,"help: cannot read %s\n",path);return 0; }
        unsigned char input[16385];size_t bytes=fread(input,1,sizeof(input),file);int bad=ferror(file);if(fclose(file))bad=1;
        if(bad || bytes>16384 || !normalize(input,bytes,helps[l][i],sizeof(helps[l][i]))) { fprintf(stderr,"help: invalid UTF-8, empty or oversized topic: %s\n",path);return 0; }
    }
    return 1;
}

static char site_messages[2][8192];
const char *site_text(unsigned kind) { return kind<2?site_messages[kind]:""; }
int site_load(const char *info,const char *bye) {
    const char *paths[]={info,bye};memset(site_messages,0,sizeof(site_messages));
    for(unsigned i=0;i<2;i++) {
        FILE *f=fopen(paths[i],"rb");if(!f) { if(errno==ENOENT)continue;return 0; }
        unsigned char input[4097];size_t n=fread(input,1,sizeof(input),f);int bad=ferror(f);if(fclose(f))bad=1;
        if(bad || n>4096)return 0;
        if(!n || (n==3 && !memcmp(input,"\xef\xbb\xbf",3)))continue;
        if(!normalize(input,n,site_messages[i],sizeof(site_messages[i])))return 0;
    }
    return 1;
}

static char hourly_messages[24][8192];
const char *hourly_greeting(unsigned hour) {
    if(hour>23)return "";
    for(unsigned i=0;i<24;i++) { unsigned h=(hour+24-i)%24;if(*hourly_messages[h])return hourly_messages[h]; }
    return "";
}
static char login_messages[3][8192];
const char *login_text(unsigned kind) { return kind<3?login_messages[kind]:""; }
int login_text_load(const char *directory) {
    memset(login_messages,0,sizeof(login_messages));memset(hourly_messages,0,sizeof(hourly_messages));
    for(unsigned h=0;h<24;h++) {
        char path[4096];if(snprintf(path,sizeof(path),"%s/greeting-%02u.txt",directory,h)>=(int)sizeof(path))return 0;
        FILE *f=fopen(path,"rb");if(!f) { if(errno==ENOENT)continue;return 0; }
        unsigned char input[4097];size_t n=fread(input,1,sizeof(input),f);int bad=ferror(f);if(fclose(f))bad=1;
        if(bad || n>4096)return 0;
        if(n==3 && !memcmp(input,"\xef\xbb\xbf",3))n=0;
        if(n && !normalize(input,n,hourly_messages[h],sizeof(hourly_messages[h])))return 0;
    }
    const char *names[]={"welcome.txt","systemnews.txt","birthday.txt"};
    for(unsigned i=0;i<3;i++) {
        char path[4096];if(snprintf(path,sizeof(path),"%s/%s",directory,names[i])>=(int)sizeof(path))return 0;
        FILE *f=fopen(path,"rb");if(!f) { if(errno==ENOENT)continue;return 0; }
        unsigned char input[4097];size_t n=fread(input,1,sizeof(input),f);int bad=ferror(f);fclose(f);
        if(bad || n>4096)return 0;
        if(n && !normalize(input,n,login_messages[i],sizeof(login_messages[i])))return 0;
    }return 1;
}
