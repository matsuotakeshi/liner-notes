#ifndef DATE_FILTER_H
#define DATE_FILTER_H
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
/* Gregorian calendar, UTC midnight, no locale or process TZ changes. */
static inline int date_filter_utc(const char *s,uint64_t *stamp) {
    if(strlen(s)!=10 || s[4]!='-' || s[7]!='-')return 0;
    for(int i=0;i<10;i++)if(i!=4 && i!=7 && (s[i]<'0' || s[i]>'9'))return 0;
    unsigned y=(s[0]-'0')*1000+(s[1]-'0')*100+(s[2]-'0')*10+s[3]-'0';
    unsigned m=(s[5]-'0')*10+s[6]-'0',d=(s[8]-'0')*10+s[9]-'0';
    static const unsigned month[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int leap=y%4==0 && (y%100!=0 || y%400==0);
    if(y<1970 || !m || m>12 || !d || d>month[m-1]+(unsigned)(m==2 && leap))return 0;
    uint64_t days=0;
    for(unsigned n=1970;n<y;n++)days+=365+(n%4==0 && (n%100!=0 || n%400==0));
    for(unsigned n=1;n<m;n++)days+=month[n-1]+(unsigned)(n==2 && leap);
    *stamp=(days+d-1)*86400;return 1;
}
/* TZ is set once, before workers start. On-disk timestamps remain Unix UTC. */
static inline const char *date_zone(void) { const char *z=getenv("TZ");return z && *z?z:"UTC"; }
static inline const char *date_pattern(void) { return !strcmp(date_zone(),"UTC")?"%Y-%m-%dT%H:%M:%SZ":"%Y-%m-%dT%H:%M:%S%z"; }
static inline int date_filter(const char *s,uint64_t *stamp) {
    uint64_t checked;if(!date_filter_utc(s,&checked))return 0;
    if(!strcmp(date_zone(),"UTC")) { *stamp=checked;return 1; }
    struct tm civil={0};civil.tm_year=atoi(s)-1900;civil.tm_mon=atoi(s+5)-1;civil.tm_mday=atoi(s+8);civil.tm_isdst=-1;
    int year=civil.tm_year,month=civil.tm_mon,day=civil.tm_mday;
    time_t value=mktime(&civil);
    if(value<0 || civil.tm_year!=year || civil.tm_mon!=month || civil.tm_mday!=day || civil.tm_hour || civil.tm_min)return 0;
    *stamp=(uint64_t)value;return 1;
}
#endif
