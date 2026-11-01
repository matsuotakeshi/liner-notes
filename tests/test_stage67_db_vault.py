"""Exercise inherited vault ACLs and repeated log rotation on the DB backend."""
from pathlib import Path
import sys
import test_stage64_vault as fixture
OriginalServer=fixture.Server
def sqlite_server(binary,store,**kwargs):
 kwargs['options']=['--database',str(Path(store).parent/'liner.sqlite'),'--userlog-keep','2']
 return OriginalServer(binary,store,**kwargs)
fixture.Server=sqlite_server
if __name__=='__main__':
 fixture.run(*sys.argv[1:]);print('PASS stage67 authoritative SQLite vault, rotating receipts/userlog and restart')
