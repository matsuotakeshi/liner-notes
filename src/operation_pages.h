#ifndef OPERATION_PAGES_H
#define OPERATION_PAGES_H
#include "article_core.h"
#include "mail_core.h"
#include "member_core.h"
#include "vault_query_core.h"
#include "door_core.h"
#include "mmm_notes.h"
#include "conference_core.h"
#include "activity_core.h"
#include "connection_core.h"
#include "download_core.h"
#include "app_limits.h"
#include "userlog.h"
typedef struct { unsigned count,next; ActivityGather rows[4]; } OperationGatherPage;
#define OPERATION_FILE_TEXT_BYTES (VAULT_CHUNK * 8)
typedef struct {
    uint32_t id,next; size_t size;
    char text[OPERATION_FILE_TEXT_BYTES];
} OperationFileText;
/* Bounded, owned results copied only after Core authorization. No display-text
 * parsing, pointers into repositories, or extra unbounded body allocation. */
typedef enum { OPERATION_PAGE_NONE,
    OPERATION_PAGE_ARTICLES,
    OPERATION_PAGE_TREE,
    OPERATION_PAGE_NOTES,
    OPERATION_PAGE_UNREAD,
    OPERATION_PAGE_BOARDS,
    OPERATION_PAGE_ARTICLE_AUDIT,
    OPERATION_PAGE_MAIL,
    OPERATION_PAGE_MAIL_FILES,
    OPERATION_PAGE_MEMBERS,
    OPERATION_PAGE_PROFILES,
    OPERATION_PAGE_PROFILE,
    OPERATION_PAGE_FILES,
    OPERATION_PAGE_FILE_DETAIL,
    OPERATION_PAGE_FILE_TEXT,
    OPERATION_PAGE_FILE_BOARDS,
    OPERATION_PAGE_FILE_CATEGORIES,
    OPERATION_PAGE_FILE_CHILDREN,
    OPERATION_PAGE_FILE_AUDIT,
    OPERATION_PAGE_DOORS,
    OPERATION_PAGE_GATHER,
    OPERATION_PAGE_PREFERENCES,
    OPERATION_PAGE_APPLICATION,
    OPERATION_PAGE_CONNECTIONS,
    OPERATION_PAGE_DOWNLOAD_HISTORY,
    OPERATION_PAGE_DOWNLOAD_STATS,
    OPERATION_PAGE_UPLOAD,
    OPERATION_PAGE_USERLOG,
    OPERATION_PAGE_USERSTATS,
    OPERATION_PAGE_USERTABLE,
    OPERATION_PAGE_CONFERENCE,
    OPERATION_PAGE_MMM_SCANLIST
} OperationPageKind;
typedef struct {
    OperationPageKind kind;
    union {
        ArticlePage articles;
        ArticleTreePage tree;
        ArticleNotesPage notes;
        ArticleUnreadPage unread;
        ArticleBoardsPage boards;
        ArticleAuditPage article_audit;
        MailPage mail;
        MailFilePage mail_files;
        MemberPage members;
        MemberProfilePage profiles;
        MemberProfile profile;
        VaultQueryFiles files;
        VaultQueryDetail file_detail;
        OperationFileText file_text;
        VaultQueryBoards file_boards;
        VaultQueryCategories file_categories;
        VaultQueryChildren file_children;
        VaultQueryAudit file_audit;
        DoorListing doors;
        OperationGatherPage gather;
        MemberPreferences preferences;
        MemberApplication application;
        DownloadHistory download_history;
        DownloadStats download_stats;
        VaultUpload upload;
        UserlogReport userlog;
        UserlogSessionPage usertable;
        struct { size_t count; ConnectionSummary rows[CONNECTION_MAX]; } connections;
        MmmScanList mmm_scanlist;
        ConferenceReply conference;
    } data;
} OperationPage;
/* Producer adapters opt in through the request, preserving legacy output. */
#define OPERATION_PAGE(result,tag,member,value) do { \
    if((result)->structured) { (result)->page.kind=OPERATION_PAGE_##tag; \
        (result)->page.data.member=(value); } \
} while(0)
#endif
