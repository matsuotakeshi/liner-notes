#ifndef LIST_NAVIGATION_CORE_H
#define LIST_NAVIGATION_CORE_H
#include <stdint.h>
/* Event-loop-owned continuation state. No UI, worker, storage or ACL grants. */
typedef enum { REPORT_NONE,REPORT_BOARDLOG,REPORT_USERLOG,REPORT_USERTABLE,
    REPORT_FILEHISTORY,REPORT_FILESTATS,REPORT_BOARDACCESS,REPORT_FILETEXT } ReportKind;
typedef struct { ReportKind kind;uint32_t target,next;int all,pending; } ReportNavigation;
void report_navigation_begin(ReportNavigation *,ReportKind,uint32_t target,int all);
/* Clear selection/next, preserving an already outstanding response. */
void report_navigation_clear(ReportNavigation *);
void report_navigation_complete(ReportNavigation *,int has_marker,uint32_t next);
int report_navigation_access_refresh(ReportNavigation *);

typedef struct { int field;uint32_t group,cursor;char query[81]; } ProfileWalkQuery;
typedef struct { int active,pending;ProfileWalkQuery query; } ProfileWalk;
/* Input is an adapter-validated PROFILEFIND query, not an actor credential. */
void profile_walk_begin(ProfileWalk *,int field,uint32_t group,const char *query);
ProfileWalkQuery profile_walk_next(ProfileWalk *);
void profile_walk_complete(ProfileWalk *,uint32_t next);
void profile_walk_reset(ProfileWalk *);

typedef struct {
    char children[16][49];unsigned count;uint32_t next;int child_view;
    char path[49],selection[49];int selecting;
} VaultNavigation;
void vault_navigation_reset(VaultNavigation *);
/* Begin only with an adapter-validated <=48-byte path. Acceptance is a worker
 * authorization result, never a decision based on the listing itself. */
void vault_navigation_begin(VaultNavigation *,const char *path);
int vault_navigation_complete(VaultNavigation *,int accepted);
void vault_children_begin(VaultNavigation *);
void vault_children_row(VaultNavigation *,unsigned index,const char *path);
const char *vault_children_select(const VaultNavigation *,unsigned index);
const char *vault_navigation_path(const VaultNavigation *);
/* Writes a 49-byte caller-owned path, returning 0 at root. No state mutation. */
int vault_navigation_parent(const VaultNavigation *,char out[49]);
#endif
