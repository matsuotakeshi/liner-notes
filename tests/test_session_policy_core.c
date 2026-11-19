#include "session_policy_core.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
int main(void) {
    const int modes[]={0,'a','g','f','v','w','b','o','p','c'};
    const PasswordAction valid[]={PASSWORD_NONE,PASSWORD_ACCOUNT,PASSWORD_ACCOUNT,
        PASSWORD_FILE_UNLOCK,PASSWORD_FILE_CONFIRM,PASSWORD_MISMATCH,PASSWORD_BOARD_UNLOCK,
        PASSWORD_STORE_OLD,PASSWORD_STORE_NEW,PASSWORD_MISMATCH};
    for(unsigned i=0;i<sizeof(modes)/sizeof(*modes);i++)for(int menu=0;menu<=1;menu++) {
        int mode=modes[i];
        assert(password_flow_action(mode,menu,0,8,0,0)==valid[i]);
        assert(password_flow_action(mode,menu,0,128,0,0)==valid[i]);
        const size_t invalid[]={0,7,129,SIZE_MAX};
        for(unsigned n=0;n<4;n++)assert(password_flow_action(mode,menu,0,invalid[n],1,1)==(mode?PASSWORD_LENGTH:PASSWORD_NONE));
        assert(password_flow_action(mode,menu,1,7,0,0)==(!mode?PASSWORD_NONE:(!menu && (mode=='a' || mode=='g'))?PASSWORD_LENGTH:PASSWORD_CANCEL));
    }
    assert(password_flow_action('p',1,0,8,1,0)==PASSWORD_UNCHANGED);
    assert(password_flow_action('p',1,0,8,0,1)==PASSWORD_STORE_NEW);
    assert(password_flow_action('c',1,0,8,0,1)==PASSWORD_CHANGE);
    assert(password_flow_action('w',0,0,8,0,1)==PASSWORD_FILE_CHANGE);
    /* Cancellation and size errors must precede even a matching confirmation. */
    assert(password_flow_action('c',1,1,8,1,1)==PASSWORD_CANCEL);
    assert(password_flow_action('c',1,0,129,1,1)==PASSWORD_LENGTH);
    const TransferNotice received[]={TRANSFER_SENT,TRANSFER_RECEIVED,TRANSFER_BATCH_SENT,
        TRANSFER_RECEIVED,TRANSFER_MAIL_UPLOAD,TRANSFER_SENT,TRANSFER_VAULT_BATCH,
        TRANSFER_RECEIVED,TRANSFER_MAIL_UPLOAD,TRANSFER_VAULT_BATCH,TRANSFER_BATCH_SENT,
        TRANSFER_MAIL_BATCH,TRANSFER_MAIL_BATCH};
    for(int mode=0;mode<13;mode++) {
        assert(transfer_notice(1,mode,0,0)==received[mode]);
        assert(transfer_notice(0,mode,0,0)==TRANSFER_FAILED);
        assert(transfer_notice(0,mode,0,1)==TRANSFER_RECOVERY);
        for(unsigned protocol=1;protocol<8;protocol++) {
            assert(transfer_notice(1,mode,protocol,0)==TRANSFER_PROTOCOL_OK);
            assert(transfer_notice(0,mode,protocol,0)==TRANSFER_PROTOCOL_FAILED);
            for(int recovery=1;recovery<=6;recovery++) {
                assert(transfer_notice(1,mode,protocol,recovery)==received[mode]);
                assert(transfer_notice(0,mode,protocol,recovery)==(recovery>=2 && recovery<=5?TRANSFER_DETAIL:TRANSFER_RECOVERY));
            }
        }
    }
    puts("PASS: all secret-input modes/bounds/confirmation and transfer outcome precedence");return 0;
}
