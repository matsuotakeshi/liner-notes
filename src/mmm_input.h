#ifndef LINER_MMM_INPUT_H
#define LINER_MMM_INPUT_H
#include <stdint.h>

typedef enum {
    MMM_UNKNOWN, MMM_AMBIGUOUS, MMM_MENU, MMM_NUMBER, MMM_HOME, MMM_UP, MMM_HELP,
    MMM_LOGIN, MMM_REGISTER, MMM_LOGOUT, MMM_OFF, MMM_NOTES, MMM_GO, MMM_OPEN,
    MMM_POST, MMM_UI
} MmmCommand;
typedef struct {
    MmmCommand command;
    char *argument;
    uint32_t number;
    int usage;
} MmmInput;

MmmInput mmm_parse(char *line, int authenticated);
int mmm_number(const char *text, uint32_t *out);
/* range defaults to every line. An empty left/right endpoint also means
 * the beginning/end. The caller decides whether complement is permitted. */
int mmm_range(const char *text, uint32_t lines, uint32_t *first, uint32_t *last,
              int *complement, const char **tail);
void mmm_command_help(MmmCommand command, const char **name, const char **usage);
#endif
