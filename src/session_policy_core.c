#include "session_policy_core.h"
PasswordAction password_flow_action(int mode,int menu,int cancel,size_t bytes,int old,int new_value) {
    if(!mode)return PASSWORD_NONE;
    int special=mode=='f' || mode=='v' || mode=='w' || mode=='b' || mode=='o' || mode=='p' || mode=='c';
    if(cancel && (special || menu))return PASSWORD_CANCEL;
    if(bytes<8 || bytes>128)return PASSWORD_LENGTH;
    switch(mode) {
    case 'f':return PASSWORD_FILE_UNLOCK;
    case 'v':return PASSWORD_FILE_CONFIRM;
    case 'w':return new_value?PASSWORD_FILE_CHANGE:PASSWORD_MISMATCH;
    case 'b':return PASSWORD_BOARD_UNLOCK;
    case 'o':return PASSWORD_STORE_OLD;
    case 'p':return old?PASSWORD_UNCHANGED:PASSWORD_STORE_NEW;
    case 'c':return new_value?PASSWORD_CHANGE:PASSWORD_MISMATCH;
    default:return PASSWORD_ACCOUNT;
    }
}
TransferNotice transfer_notice(int success,int receive,unsigned protocol,int recovery) {
    if(!success && recovery>=2 && recovery<=5)return TRANSFER_DETAIL;
    if(protocol && !recovery)return success?TRANSFER_PROTOCOL_OK:TRANSFER_PROTOCOL_FAILED;
    if(success) {
        switch(receive) {
        case 11:case 12:return TRANSFER_MAIL_BATCH;
        case 6:case 9:return TRANSFER_VAULT_BATCH;
        case 4:case 8:return TRANSFER_MAIL_UPLOAD;
        case 2:case 10:return TRANSFER_BATCH_SENT;
        case 0:case 5:return TRANSFER_SENT;
        default:return TRANSFER_RECEIVED;
        }
    }
    return recovery?TRANSFER_RECOVERY:TRANSFER_FAILED;
}
