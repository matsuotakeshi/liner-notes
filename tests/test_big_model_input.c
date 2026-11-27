#include "big_model_input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char command[]="r   100001",long_command[]="protocol ZMODEM",bare[]="!",plain[]="";
    BigModelCommand parsed=big_model_command(command);
    assert(parsed.key=='R' && !strcmp(parsed.argument,"100001"));
    assert(!big_model_command(long_command).key && !strcmp(big_model_command(long_command).argument,"ZMODEM"));
    assert(big_model_command(bare).key=='!' && !big_model_command(plain).key);
    char header[180];
    assert(big_model_format_header(header,sizeof(header),1,0,"alice","test"));
    assert(!strcmp(header,"00001  --/--/-- --:--:--  alice  test\r\n"));
    assert(big_model_format_header(header,sizeof(header),100001,0,"bob","reply"));
    assert(!strncmp(header,"100001  ",8));
    assert(!big_model_format_header(header,5,100001,0,"bob","reply"));
    uint64_t old,full;
    assert(big_model_date("24-02-29",&old) && big_model_date("2024-02-29",&full) && old==full);
    assert(!big_model_date("2025-02-29",&full));
    assert(big_model_telegram_valid("12345678901234567890\xe3\x81\x82"));
    assert(!big_model_telegram_valid("123456789012345678901\xe3\x81\x82"));
    assert(!big_model_telegram_valid("\xc0\x80") && !big_model_telegram_valid("foo\033bar"));
    puts("PASS BIG-Model: command boundaries, dates, nontruncated article IDs, UTF-8 telegram length");
    return 0;
}
