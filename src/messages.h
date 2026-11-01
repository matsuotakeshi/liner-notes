#ifndef MESSAGES_H
#define MESSAGES_H
typedef enum { LANGUAGE_JA, LANGUAGE_EN, LANGUAGE_COUNT } Language;
typedef enum {
#define MESSAGE(id, text) MSG_##id,
#include "locales/en.def"
#undef MESSAGE
    MSG_COUNT
} MessageId;
const char *message_text(Language language, MessageId id);
const char *language_code(Language language);
int language_parse(const char *code, Language *language);
#endif
