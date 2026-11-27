#include "ktbbs_input.h"
#include <stdio.h>
#include <string.h>

static int digit(char c) { return c >= '0' && c <= '9'; }

KtbbsInput ktbbs_input(const char *line) {
    while (*line == ' ' || *line == '\t') line++;
    if (!*line) return (KtbbsInput){'\r', line};
    if (!strcmp(line, "//")) return (KtbbsInput){'/', line + 2};
    unsigned char key = (unsigned char)*line++;
    if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
    if (!((key >= 'A' && key <= 'Z') || strchr("0?#@+-", key)))
        return (KtbbsInput){0, line};
    if (*line && *line != ' ' && *line != '\t' &&
        !(key == 'N' && *line == '@') && !(key == 'T' && digit(*line)))
        return (KtbbsInput){0, line};
    while (*line == ' ' || *line == '\t') line++;
    return (KtbbsInput){key, line};
}

int ktbbs_id(const char *text, uint32_t *number) {
    if (*text == '#') text++;
    if (!digit(*text)) return 0;
    uint64_t value = 0;
    for (; digit(*text); text++) {
        value = value * 10 + (unsigned)(*text - '0');
        if (value > UINT32_MAX) return 0;
    }
    if (*text || !value) return 0;
    *number = (uint32_t)value;
    return 1;
}

int ktbbs_date(const char *text, char normalized[11]) {
    /* Two-digit dates use the 1970–2069 window. Calendar validation is shared. */
    if (strlen(text) == 8 && digit(text[0]) && digit(text[1]) &&
        text[2] == '/' && digit(text[3]) && digit(text[4]) &&
        text[5] == '/' && digit(text[6]) && digit(text[7])) {
        unsigned year = (unsigned)(text[0] - '0') * 10 + (unsigned)(text[1] - '0');
        snprintf(normalized, 11, "%s%.2s-%.2s-%.2s", year >= 70 ? "19" : "20",
                 text, text + 3, text + 6);
        return 1;
    }
    if (strlen(text) != 10) return 0;
    memcpy(normalized, text, 11);
    return 1;
}
