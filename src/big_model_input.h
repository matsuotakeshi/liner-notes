#ifndef LINER_BIG_MODEL_INPUT_H
#define LINER_BIG_MODEL_INPUT_H

#include <stddef.h>
#include <stdint.h>

typedef struct { int key; char *argument; } BigModelCommand;
BigModelCommand big_model_command(char *line);
int big_model_date(const char *value,uint64_t *since);
int big_model_telegram_valid(const char *text);
int big_model_format_header(char *out,size_t capacity,uint32_t id,uint64_t timestamp,
                            const char *author,const char *title);

#endif
