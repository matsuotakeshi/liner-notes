/* Legacy connector response formatting only; admission and mutation decisions
 * live in link_core.c. No SQL or signature processing in this adapter. */
static void link_result(LinkOutcome outcome,BoardResult *r) {
    const char *message="ERR link action";
    switch(outcome) {
    case LINK_DENIED:message="ERR link denied";break;
    case LINK_REVOKED:r->revoked=1;message="ERR session revoked";break;
    case LINK_BOARD_STORAGE:message="ERR board policy storage";break;
    case LINK_EXTRAS_STORAGE:message="ERR extras storage";break;
    case LINK_MEMBER_STORAGE:message="ERR member storage unavailable";break;
    case LINK_STORAGE:message="ERR link storage; restart inspection required";break;
    case LINK_IDENTITY:message="ERR link identity";break;
    case LINK_POSTING_DENIED:message="ERR link posting denied";break;
    case LINK_REVISION:message="ERR link revision";break;
    case LINK_PREDECESSOR:message="ERR link missing predecessor";break;
    case LINK_UNAVAILABLE:message="ERR link article unavailable";break;
    case LINK_TITLE:message="ERR link title";break;
    case LINK_CONFLICT:message="ERR link checkpoint conflicts with immutable article";break;
    case LINK_PARENT:message="ERR link parent unavailable";break;
    case LINK_POST_LOGIN:message="ERR login required";break;
    case LINK_POST_VERSION:message="ERR member posting needs RTBOARD3";break;
    case LINK_POST_STORAGE:message="ERR storage unavailable";break;
    case LINK_POST_PARENT:message="ERR no parent";break;
    case LINK_POST_BODY:message="ERR body limit";break;
    case LINK_POST_FULL:message="ERR board full";break;
    case LINK_POST_DRAFT:message="ERR draft";break;
    case LINK_POST_CAPACITY:message="ERR legacy board capacity; compact to RTBOARD4 first";break;
    case LINK_POST_UNCERTAIN:message="ERR storage uncertain; restart inspection required";break;
    default:break;
    }
    snprintf(r->text,sizeof(r->text),"%s\r\n",message);
}
static void link_execute(const BoardRequest *q,BoardResult *r) {
    LinkActor actor={q->user,q->user_epoch,q->board_grants};LinkOutcome outcome;
    if(q->action=='S' || q->action=='M' || q->action=='B') {
        LinkPage page;
        outcome=link_core_scan(&actor,q->board,q->action=='S'?LINK_SNAPSHOT:q->action=='B'?LINK_BATCH:LINK_METADATA,
                               q->cursor,&page,r->draft_body,sizeof(r->draft_body));
        if(outcome==LINK_OK) {
            size_t used=0;
            for(unsigned i=0;i<page.count;i++) {
                const LinkItem *item=&page.items[i];
                const char *state=item->state==LINK_WITHDRAWN?"WITHDRAWN":item->state==LINK_HIDDEN?"HIDDEN":"LIVE";
                size_t body_size=q->action=='S'?page.body_size:0;
                int n=snprintf(r->text+used,sizeof(r->text)-used,"LINK ITEM %u %u %llu %s %s %s %u\r\nAUTHOR %s\r\nTITLE %s\r\nBODY %zu\r\n%s",
                    item->id,item->parent,(unsigned long long)item->created,state,item->origin,item->key,item->revision,item->author,item->title,body_size,body_size?r->draft_body:"");
                if(n<0 || (size_t)n>=sizeof(r->text)-used) { link_repository_fail();outcome=LINK_STORAGE;break; }
                used+=(size_t)n;
            }
            if(outcome==LINK_OK && page.end)snprintf(r->text+used,sizeof(r->text)-used,"LINK END\r\n");
        }
        OPENSSL_cleanse(r->draft_body,sizeof(r->draft_body));
    } else if(q->action=='L') {
        LinkReference reference;outcome=link_core_lookup(&actor,q->board,q->link_origin,q->link_article,&reference);
        if(outcome==LINK_OK)snprintf(r->text,sizeof(r->text),"LINK LOOKUP %u %u\r\n",reference.id,reference.revision);
    } else {
        LinkEvent event={q->board,q->cursor,q->number,q->since,(char)q->action,q->link_origin,q->link_article,q->username,q->title,q->body};
        LinkApplied applied;outcome=link_core_apply(&actor,&event,&applied);
        if(outcome==LINK_OK) {
            if(applied.created)r->selected=applied.reference.id;
            snprintf(r->text,sizeof(r->text),"LINK APPLIED %u %u\r\n",applied.reference.id,applied.reference.revision);
        }
    }
    if(outcome!=LINK_OK) { OPENSSL_cleanse(r->text,sizeof(r->text));link_result(outcome,r); }
}
