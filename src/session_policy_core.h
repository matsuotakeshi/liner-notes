#ifndef SESSION_POLICY_CORE_H
#define SESSION_POLICY_CORE_H
#include <stddef.h>
/* Pure decisions: no Session, secrets, UI, handles, persistence or authorization.
 * Equality flags come from the owning adapter; lengths are bytes, not characters. */
typedef enum { PASSWORD_NONE,PASSWORD_CANCEL,PASSWORD_LENGTH,PASSWORD_STORE_OLD,
    PASSWORD_STORE_NEW,PASSWORD_FILE_CONFIRM,PASSWORD_CHANGE,PASSWORD_MISMATCH,
    PASSWORD_UNCHANGED,PASSWORD_FILE_UNLOCK,PASSWORD_FILE_CHANGE,
    PASSWORD_BOARD_UNLOCK,PASSWORD_ACCOUNT } PasswordAction;
PasswordAction password_flow_action(int mode,int menu,int cancel,size_t bytes,int matches_old,int matches_new);
typedef enum { TRANSFER_DETAIL,TRANSFER_PROTOCOL_OK,TRANSFER_PROTOCOL_FAILED,
    TRANSFER_MAIL_BATCH,TRANSFER_VAULT_BATCH,TRANSFER_MAIL_UPLOAD,TRANSFER_BATCH_SENT,
    TRANSFER_RECEIVED,TRANSFER_SENT,TRANSFER_RECOVERY,TRANSFER_FAILED } TransferNotice;
/* receive/protocol retain existing runtime wire values; caller validates protocol 0..7. */
TransferNotice transfer_notice(int success,int receive,unsigned protocol,int recovery);
#endif
