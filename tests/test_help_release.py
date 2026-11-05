"""Read shipped bilingual HELP through real sessions, including LOGOUT behavior."""
from pathlib import Path
import json,sys,tempfile,os,itertools
from test_board import Server
from test_runtime import Client
from test_members import auth
from test_ui import Menu

root=Path(__file__).resolve().parents[1]
binary=str(Path(sys.argv[1]).resolve())
helpdir=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else root/'help'
with tempfile.TemporaryDirectory(prefix='help-release-') as temp:
    data=Path(temp)
    for lang,external in itertools.product(('ja','en'),(True,False)):
        # With no help/ directory, runtime must use the same corrected built-in text.
        oldcwd=Path.cwd()
        os.chdir(data)
        try:
            suffix=lang+str(external)
            server=Server(binary,data/('notes-'+suffix),members=data/('members-'+suffix),language=lang,options=['--help-dir',str(helpdir)] if external else [])
        finally:os.chdir(oldcwd)
        c=Client(server.port)
        try:
            auth(c,'reader',True)
            for topic,required in {
                'CONTENT':['SIGOP','SUBTREE','物理回収' if lang=='ja' else 'reclamation'],
                'SETTINGS':['LOGOUT','ゲストへ戻' if lang=='ja' else 'returns to guest','QUIT'],
                'UNREAD':['CHECKMODE','AUTO','MANUAL','完了前' if lang=='ja' else 'before completion'],
            }.items():
                c.send(('HELP '+topic+'\n').encode());lines=[]
                while True:
                    line=c.line()
                    if line==b'END HELP':break
                    lines.append(line)
                text=b'\n'.join(lines).decode('utf8')
                assert all(s in text for s in required),(lang,topic,text)
                assert 'stage67' not in text
            c.send(b'LOGOUT\n');c.expect(b'LOGGED OUT')
            c.send(b'PING\n');c.expect(b'PONG')
        finally:c.close();server.close()
    server=Server(binary,data/'version',menu=True,language='en')
    c=Menu(server.port)
    try:
        c.until();text=c.cmd('V')
        version=json.loads((root/'docs/manual/manifest.json').read_text(encoding='utf8'))['version']
        assert 'Liner notes '+version in text and '(stage' not in text,text
    finally:c.close();server.close()
print('PASS external and built-in ja/en HELP, SIGOP/subtree descriptions, logout keeps connection, release version')
