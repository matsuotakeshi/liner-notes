"""Reject invalid options; failed exec and empty/limited menus remain usable."""
from pathlib import Path
import subprocess,sys,tempfile,time
from test_board import Server
from test_telnet import Client

def run(binary):
    binary=str(Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='ln-door-config-') as tmp:
        p=Path(tmp);game=p/'bad-exec';game.write_text('#!/nonexistent/liner-notes-interpreter\n');game.chmod(0o700)
        config=p/'doors';base=f'1|0|30|{game}|{p}|Broken executable'
        for bad in ['','dorinfo1:0','dorinfo1:33','dorinfo1:-1','dorinfo1:1junk','dorinfo1:1|extra','none:0','none:999999999999999999999','unknown:1']:
            config.write_text(base+'|'+bad+'\n')
            r=subprocess.run([binary,'--doors',str(config),'0',str(p/'notes')],capture_output=True,timeout=10)
            assert r.returncode and b'LISTEN ' not in r.stdout,(bad,r.stdout,r.stderr)
        for options in ['none:1','dorinfo1:1']:
            config.write_text(base+'|'+options+'\n')
            s=Server(binary,p/'notes',members=p/'members',menu=True,telnet=True,language='en',options=['--doors',str(config)])
            a=Client(s.port)
            try:
                a.until();a.send(b'REGISTER alice\r\nconfig-password\r\n' if options=='none:1' else b'LOGIN alice\r\nconfig-password\r\n');assert b'Logged in' in a.until()
                a.cmd('GAMES',b'0:main] > ')
                for _ in range(2):
                    out=a.cmd('1',b'0:main] > ');assert b'Back to BBS' in out,out;time.sleep(.1)
                assert not list(p.glob('.liner-door-run-*'))
            finally:a.close();s.close()
        s=Server(binary,p/'notes',members=p/'members',menu=True,telnet=True,language='en')
        a=Client(s.port)
        try:
            a.until();a.send(b'LOGIN alice\r\nconfig-password\r\n');a.until()
            assert b'No games are available' in a.cmd('GAMES',b'0:main] > ')
            assert b'MAIN' in a.cmd('0')
        finally:a.close();s.close()
    print('PASS Door config validation, failed exec cleanup/retry, slots without dropfiles and empty menu')

if __name__=='__main__':run(sys.argv[1])
