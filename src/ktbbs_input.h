#ifndef LINER_KTBBS_INPUT_H
#define LINER_KTBBS_INPUT_H

#include <stdint.h>

typedef struct {
    int key;
    const char *argument;
} KtbbsInput;

/* Read one command at a prompt. Never call this for passwords or body text. */
KtbbsInput ktbbs_input(const char *line);
int ktbbs_id(const char *text, uint32_t *number);
int ktbbs_date(const char *text, char normalized[11]);

#endif
