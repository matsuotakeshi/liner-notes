"""Binary peer fixture used by Telnet connector tests; stdout is protocol only."""
import pathlib,sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from legacy_transfer import LegacyEngine
mode,action,name=sys.argv[1:]
e=LegacyEngine(mode,20,filename=name)
if action=='send':e.send(pathlib.Path(name),0,1,lambda:None)
else:e.receive(pathlib.Path.cwd(),0,1,lambda:None)
