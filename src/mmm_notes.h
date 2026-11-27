#ifndef LINER_MMM_NOTES_H
#define LINER_MMM_NOTES_H
#include "article_core.h"

enum { MMM_LABEL_MAX=31, MMM_SCAN_MAX=64, MMM_SIGNATURE_MAX=2048 };
typedef struct { int automatic; char body[MMM_SIGNATURE_MAX+1]; } MmmSignature;
ArticleOutcome mmm_signature_get(const ArticleActor *,MmmSignature *);
ArticleOutcome mmm_signature_set(const ArticleActor *,int action,const char *,MmmSignature *);
typedef enum { MMM_SCAN_NEW, MMM_SCAN_INDEX, MMM_SCAN_FALLBACK } MmmScanMode;
typedef struct { uint32_t board; char label[MMM_LABEL_MAX+1]; int exclude; MmmScanMode mode; } MmmBoardTarget;
typedef struct { unsigned count; MmmBoardTarget targets[MMM_SCAN_MAX]; } MmmScanTargets;
typedef struct { uint32_t board; char label[MMM_LABEL_MAX+1],title[81]; MmmScanMode mode; } MmmScanBoard;
typedef struct { int custom; unsigned count; MmmScanBoard rows[MMM_SCAN_MAX]; } MmmScanList;

/* Worker-owned MMM operations. Label lookup can return a board requiring
 * its existing password challenge. Scanlists contain only readable boards
 * and never turn an inaccessible personal list into the public default. */
ArticleOutcome mmm_note_label_resolve(const ArticleActor *,const char *,uint32_t *);
/* replacement=NULL reads; empty replacement removes. Global admin only. */
ArticleOutcome mmm_note_label(const ArticleActor *,uint32_t,const char *replacement,char out[MMM_LABEL_MAX+1]);
ArticleOutcome mmm_note_scanlist(const ArticleActor *,MmmScanList *);
/* Empty list resets to default. Validation and persistence are all-or-nothing. */
ArticleOutcome mmm_note_scanlist_save(const ArticleActor *,const MmmScanTargets *);
ArticleOutcome flat_scanlist(const ArticleActor *,MmmScanList *);
ArticleOutcome flat_scanlist_save(const ArticleActor *,const MmmScanTargets *);
#endif
