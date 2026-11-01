#include "identity_legacy.h"
#include "members.h"
#include "moderation.h"
#include <stdio.h>
#include <string.h>
static int resolve(void *context,uint32_t user,uint32_t epoch,Principal *out) {
    (void)context; memset(out,0,sizeof(*out));
    if(members_broken() || moderation_broken() || !members_name(user) ||
       moderation_suspended(user) || moderation_epoch(user)!=epoch) return 0;
    snprintf(out->subject,sizeof(out->subject),"legacy-member:%u",user);
    out->capabilities=CAP_VAULT_READ|CAP_VAULT_UPLOAD;out->level=(unsigned)members_level(user);out->groups=members_groups(user);for(unsigned i=0;i<3;i++)out->group_high[i]=members_group_word(user,i+1);
    if(moderation_admin(user)) out->capabilities|=CAP_VAULT_MANAGE;
    return 1;
}
IdentityProvider identity_legacy_provider(void) { IdentityProvider p={NULL,resolve}; return p; }
