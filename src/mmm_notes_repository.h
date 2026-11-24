#ifndef LINER_MMM_NOTES_REPOSITORY_H
#define LINER_MMM_NOTES_REPOSITORY_H
#include "mmm_notes.h"

#define MMM_NOTES_SCHEMA \
    "CREATE TABLE IF NOT EXISTS mmm_board_labels(board INTEGER PRIMARY KEY CHECK(board BETWEEN 1 AND 1024)," \
      "label TEXT NOT NULL UNIQUE CHECK(length(label) BETWEEN 1 AND 31));" \
    "CREATE TABLE IF NOT EXISTS mmm_scanlist(user INTEGER NOT NULL CHECK(user>0)," \
      "position INTEGER NOT NULL CHECK(position BETWEEN 1 AND 64)," \
      "board INTEGER NOT NULL CHECK(board BETWEEN 1 AND 1024)," \
      "PRIMARY KEY(user,position),UNIQUE(user,board));"
int mmm_notes_label_get(uint32_t board,char out[MMM_LABEL_MAX+1]);
int mmm_notes_label_lookup(const char *,uint32_t *board);
/* 1 success, 0 storage failure, -1 duplicate. */
int mmm_notes_label_save(uint32_t board,const char *);
int mmm_notes_scanlist_load(uint32_t user,uint32_t out[MMM_SCAN_MAX],unsigned *count);
int mmm_notes_scanlist_replace(uint32_t user,const uint32_t *,unsigned count);
#endif
