#ifndef OPERATION_PAGES_H
#define OPERATION_PAGES_H
#include "article_core.h"
#include "mail_core.h"
#include "member_core.h"
#include "vault_query_core.h"
#include "door_core.h"
#include "mmm_notes.h"
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
    OPERATION_PAGE_FILE_BOARDS,
    OPERATION_PAGE_FILE_CATEGORIES,
    OPERATION_PAGE_FILE_CHILDREN,
    OPERATION_PAGE_FILE_AUDIT,
    OPERATION_PAGE_DOORS,
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
        VaultQueryBoards file_boards;
        VaultQueryCategories file_categories;
        VaultQueryChildren file_children;
        VaultQueryAudit file_audit;
        DoorListing doors;
        MmmScanList mmm_scanlist;
    } data;
} OperationPage;
/* Producer adapters opt in through the request, preserving legacy output. */
#define OPERATION_PAGE(result,tag,member,value) do { \
    if((result)->structured) { (result)->page.kind=OPERATION_PAGE_##tag; \
        (result)->page.data.member=(value); } \
} while(0)
#endif
