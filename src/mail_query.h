#ifndef MAIL_QUERY_H
#define MAIL_QUERY_H
/* Shared listing modes; their numeric values are part of the worker protocol. */
typedef enum { MAIL_LIST_ALL, MAIL_LIST_SENT, MAIL_LIST_UNREAD, MAIL_LIST_INBOX,
               MAIL_LIST_SEARCH, MAIL_LIST_NEWS } MailListMode;
#endif
