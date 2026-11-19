#ifndef LINER_UI_PROFILE_H
#define LINER_UI_PROFILE_H
#include <string.h>
#define UI_PREFERENCE_SCHEMA "CREATE TABLE IF NOT EXISTS ui_preferences(user INTEGER PRIMARY KEY CHECK(user>0),profile TEXT NOT NULL CHECK(length(profile) BETWEEN 1 AND 31));"
static inline int ui_profile_valid(const char *name) {
    if(!name || !*name || strlen(name)>31)return 0;
    return strspn(name,"abcdefghijklmnopqrstuvwxyz0123456789-")==strlen(name);
}
#endif
