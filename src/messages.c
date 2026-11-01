#include "messages.h"
#include <string.h>
static const char *const english[MSG_COUNT]={
#define MESSAGE(id, text) [MSG_##id]=text,
#include "locales/en.def"
#undef MESSAGE
};
static const char *const japanese[MSG_COUNT]={
#define MESSAGE(id, text) [MSG_##id]=text,
#include "locales/ja.def"
#undef MESSAGE
};
const char *message_text(Language language, MessageId id) {
    if(id<0 || id>=MSG_COUNT) return "";
    const char *text=language==LANGUAGE_JA?japanese[id]:NULL;
    return text?text:english[id];
}
const char *language_code(Language language) { return language==LANGUAGE_JA?"ja":"en"; }
int language_parse(const char *code, Language *language) {
    if((code[0]=='j' || code[0]=='J') && (code[1]=='a' || code[1]=='A') && !code[2]) *language=LANGUAGE_JA;
    else if((code[0]=='e' || code[0]=='E') && (code[1]=='n' || code[1]=='N') && !code[2]) *language=LANGUAGE_EN;
    else return 0;
    return 1;
}
