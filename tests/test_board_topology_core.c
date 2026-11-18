#include "board_topology_core.h"
#include "board_topology_repository.h"
#include "board_policy_repository.h"
#include "board_policy.h"
#include "extras.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static CatalogOutcome identity;
static int admin,managed,broken,extra_error,ready,prepare_ok,delete_ok,move_ok,cascade_ok;
static unsigned prepares,deletes,moves,cascades,reads,admin_checks;
static unsigned char present[CATALOG_MAX+1],menus[CATALOG_MAX+1];
static uint32_t parents[CATALOG_MAX+1],article_limit[CATALOG_MAX+1];
static uint64_t byte_limit[CATALOG_MAX+1];
static BoardTopologyArticle articles[16];static uint32_t count,read_fail;
static BoardDeletion saved_plan[CATALOG_MAX];static size_t plan_count;
static uint32_t moved_from,moved_to,moved_root,moved_through;static int moved_branch;
CatalogOutcome catalog_core_admin(const CatalogActor *a) {
    assert(a && a->user==1 && a->epoch==8);admin_checks++;
    return identity!=CATALOG_CORE_OK?identity:admin?CATALOG_CORE_OK:CATALOG_CORE_DENIED;
}
CatalogOutcome catalog_core_manage(const CatalogActor *a,uint32_t board) {
    assert(a && a->user==1 && a->epoch==8);
    return identity!=CATALOG_CORE_OK?identity:catalog_exists(board) && managed?CATALOG_CORE_OK:CATALOG_CORE_DENIED;
}
int catalog_exists(uint32_t b) { return b && b<=CATALOG_MAX && present[b]; }
uint32_t catalog_parent(uint32_t b) { return parents[b]; }
unsigned catalog_depth(uint32_t b) { unsigned d=0;while(parents[b]) { d++;b=parents[b]; }return d; }
int catalog_under(uint32_t b,uint32_t root) {
    if(!catalog_exists(b) || !catalog_exists(root))return 0;
    while(b) { if(b==root)return 1;b=parents[b]; }return 0;
}
int board_policy_repository_broken(void) { return broken; }
int policy_broken(void) { return 0; }
int extras_broken(void) { return extra_error; }
int board_topology_repository_ready(void) { return ready; }
int board_policy_repository_prepare(void) { prepares++;return prepare_ok; }
int board_policy_repository_has_articles(uint32_t b) {
    for(uint32_t i=1;i<=count;i++)if(articles[i].board==b)return 1;
    return 0;
}
int policy_menu(uint32_t b) { return menus[b]; }
uint32_t policy_max_articles(uint32_t b) { return article_limit[b]; }
uint64_t policy_max_bytes(uint32_t b) { return byte_limit[b]; }
uint32_t board_topology_repository_count(void) { return count; }
int board_topology_repository_article(uint32_t id,BoardTopologyArticle *out) {
    assert(id && id<=count);reads++;*out=articles[id];return id!=read_fail;
}
int board_topology_repository_delete(uint32_t b,uint32_t actor) {
    assert(b==5 && actor==1 && prepares && admin_checks);deletes++;return delete_ok;
}
int board_topology_repository_move(uint32_t actor,uint32_t from,uint32_t to,uint32_t root,uint32_t through,int branch) {
    assert(actor==1 && prepares && admin_checks);moves++;
    moved_from=from;moved_to=to;moved_root=root;moved_through=through;moved_branch=branch;return move_ok;
}
int board_topology_repository_cascade(uint32_t actor,const BoardDeletion *plan,size_t n) {
    assert(actor==1 && prepares && admin_checks && n<=CATALOG_MAX);cascades++;
    memcpy(saved_plan,plan,n*sizeof(*plan));plan_count=n;return cascade_ok;
}
static CatalogActor reset(void) {
    identity=CATALOG_CORE_OK;admin=managed=ready=prepare_ok=delete_ok=move_ok=cascade_ok=1;
    broken=extra_error=0;prepares=deletes=moves=cascades=reads=admin_checks=0;
    memset(present,0,sizeof(present));memset(parents,0,sizeof(parents));memset(menus,0,sizeof(menus));
    memset(article_limit,0,sizeof(article_limit));memset(byte_limit,0,sizeof(byte_limit));
    for(unsigned i=1;i<=5;i++) { present[i]=1;parents[i]=i==1?0:1; }parents[4]=2;
    memset(articles,0,sizeof(articles));count=5;read_fail=0;
    articles[1]=(BoardTopologyArticle){2,0,1,10,0};
    articles[2]=(BoardTopologyArticle){2,1,1,20,0};
    articles[3]=(BoardTopologyArticle){2,2,1,30,1}; /* withdrawn; still moved */
    articles[4]=(BoardTopologyArticle){2,0,4,40,0}; /* unrelated source root */
    articles[5]=(BoardTopologyArticle){3,0,5,5,0};
    memset(saved_plan,0,sizeof(saved_plan));plan_count=0;
    moved_from=moved_to=moved_root=moved_through=0;moved_branch=0;
    return (CatalogActor){1,8};
}
int main(void) {
    CatalogActor a=reset();
    assert(board_topology_capacity(2,0,0)==1);
    article_limit[2]=3;byte_limit[2]=70;
    assert(board_topology_capacity(2,0,0)==1);
    assert(board_topology_capacity(2,1,0)==0);
    assert(board_topology_capacity(2,0,1)==0);
    byte_limit[2]=article_limit[2]=0;
    assert(board_topology_capacity(2,1000,100000)==1);
    assert(board_topology_capacity(2,0,UINT64_MAX)==-1);
    read_fail=2;assert(board_topology_capacity(2,0,0)==-1);
    assert(!prepares && !deletes && !moves && !cascades);
    puts("PASS: shared quotas exclude withdrawn, include other live rows, handle zero/boundaries/read failure");

    a=reset();assert(board_topology_move(&a,2,1,3,0)==TOPOLOGY_OK);
    assert(moves==1 && moved_from==2 && moved_to==3 && moved_root==1 && moved_through==5 && !moved_branch);
    assert(articles[2].parent==1 && articles[3].parent==2 && articles[3].removed);
    a=reset();assert(board_topology_move(&a,2,2,3,1)==TOPOLOGY_OK && moved_branch && moved_root==2);
    puts("PASS: whole-thread and branch plans retain IDs and delegate durable replay once");

    a=reset();assert(board_topology_move(&a,2,2,3,0)==TOPOLOGY_MOVE_TARGET);
    assert(board_topology_move(&a,2,0,3,0)==TOPOLOGY_MOVE_TARGET);
    assert(board_topology_move(&a,2,6,3,0)==TOPOLOGY_MOVE_TARGET);
    assert(board_topology_move(&a,2,5,3,0)==TOPOLOGY_MOVE_TARGET);
    assert(board_topology_move(&a,2,1,2,0)==TOPOLOGY_MOVE_TARGET);
    assert(board_topology_move(&a,2,1,100,0)==TOPOLOGY_MOVE_TARGET);
    menus[3]=1;assert(board_topology_move(&a,2,1,3,1)==TOPOLOGY_MOVE_TARGET && !moves);
    puts("PASS: wrong-board, missing, non-root, same-board and menu targets rejected");

    a=reset();article_limit[3]=2;assert(board_topology_move(&a,2,1,3,0)==TOPOLOGY_FULL && !moves);
    article_limit[3]=3;byte_limit[3]=34;assert(board_topology_move(&a,2,1,3,0)==TOPOLOGY_FULL && !moves);
    byte_limit[3]=35;assert(board_topology_move(&a,2,1,3,0)==TOPOLOGY_OK && moves==1);
    a=reset();article_limit[3]=2;byte_limit[3]=25;
    assert(board_topology_move(&a,2,2,3,1)==TOPOLOGY_OK); /* excludes unrelated root and removed leaf */
    puts("PASS: destination count/byte limits account for existing rows and selected branch only");

    a=reset();read_fail=2;assert(board_topology_move(&a,2,1,3,0)==TOPOLOGY_POLICY_STORAGE && !moves);
    a=reset();articles[3].parent=3;assert(board_topology_move(&a,2,2,3,1)==TOPOLOGY_POLICY_STORAGE && !moves);
    a=reset();move_ok=0;assert(board_topology_move(&a,2,1,3,0)==TOPOLOGY_POLICY_STORAGE && moves==1);
    puts("PASS: uncertain metadata, corrupt parent chain and durable move failure are not acknowledged");

    a=reset();assert(board_topology_delete(&a,1,0)==TOPOLOGY_NOT_EMPTY);
    assert(board_topology_delete(&a,2,0)==TOPOLOGY_NOT_EMPTY);
    count=0;assert(board_topology_delete(&a,2,0)==TOPOLOGY_NOT_EMPTY && !deletes); /* child */
    assert(board_topology_delete(&a,5,0)==TOPOLOGY_OK && deletes==1);
    a=reset();for(unsigned i=1;i<=count;i++)articles[i].removed=1;
    assert(board_topology_delete(&a,2,0)==TOPOLOGY_NOT_EMPTY && !deletes);
    delete_ok=0;assert(board_topology_delete(&a,5,0)==TOPOLOGY_POLICY_STORAGE && deletes==1);
    puts("PASS: root, child and withdrawn-only boards stay nonempty; delete failures propagate");

    a=reset();assert(board_topology_delete(&a,1,1)==TOPOLOGY_INVALID);
    assert(board_topology_delete(&a,100,1)==TOPOLOGY_INVALID && !prepares);
    assert(board_topology_delete(&a,2,1)==TOPOLOGY_OK && cascades==1 && plan_count==2);
    assert(saved_plan[0].board==2 && saved_plan[0].depth==1 && saved_plan[1].board==4 && saved_plan[1].depth==2);
    a=reset();cascade_ok=0;assert(board_topology_delete(&a,2,1)==TOPOLOGY_SETTINGS_STORAGE && cascades==1);
    puts("PASS: complete scoped cascade plan with depth, root protection and recovery failure");

    a=reset();admin=0;
    assert(board_topology_move(&a,2,1,3,0)==TOPOLOGY_DENIED);
    assert(board_topology_delete(&a,5,0)==TOPOLOGY_DENIED);
    assert(board_topology_delete(&a,2,1)==TOPOLOGY_DENIED);
    assert(!moves && !deletes && !cascades); /* managed SIGOP is not an administrator */
    a=reset();managed=0;assert(board_topology_move(&a,2,1,3,0)==TOPOLOGY_DENIED && !prepares);
    a=reset();prepare_ok=0;
    assert(board_topology_move(&a,2,1,3,0)==TOPOLOGY_POLICY_STORAGE);
    assert(board_topology_delete(&a,5,0)==TOPOLOGY_POLICY_STORAGE);
    assert(board_topology_delete(&a,2,1)==TOPOLOGY_INVALID);
    puts("PASS: SIGOP cannot gain destructive authority; preparation ordering and legacy errors preserved");

    const CatalogOutcome errors[]={CATALOG_CORE_LOGIN_REQUIRED,CATALOG_CORE_REVOKED,CATALOG_CORE_MEMBER_STORAGE,CATALOG_CORE_MODERATION_STORAGE,CATALOG_CORE_POLICY_STORAGE,CATALOG_CORE_STORAGE};
    const BoardTopologyOutcome outcomes[]={TOPOLOGY_DENIED,TOPOLOGY_REVOKED,TOPOLOGY_MEMBER_STORAGE,TOPOLOGY_MODERATION_STORAGE,TOPOLOGY_POLICY_STORAGE,TOPOLOGY_CATALOG_STORAGE};
    for(unsigned i=0;i<sizeof(errors)/sizeof(*errors);i++) {
        a=reset();identity=errors[i];
        assert(board_topology_move(&a,2,1,3,0)==outcomes[i]);
        assert(board_topology_delete(&a,5,0)==outcomes[i]);
        assert(board_topology_delete(&a,2,1)==outcomes[i]);
        assert(!prepares && !reads && !moves && !deletes && !cascades);
    }
    for(unsigned i=0;i<3;i++) {
        a=reset();if(i==0)broken=1;else if(i==1)extra_error=1;else ready=0;
        BoardTopologyOutcome wanted=i==0?TOPOLOGY_POLICY_STORAGE:i==1?TOPOLOGY_EXTRAS_STORAGE:TOPOLOGY_SETTINGS_STORAGE;
        assert(board_topology_move(&a,2,1,3,0)==wanted);
        assert(board_topology_delete(&a,5,0)==wanted);
        assert(board_topology_delete(&a,2,1)==wanted);
        assert(!prepares && !moves && !deletes && !cascades);
    }
    puts("PASS: every mutation rechecks identity and storage before repository side effects");
    return 0;
}
