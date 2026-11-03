"""Optional real-game acceptance; argv[2] must be a configured, freshly reset test world."""
from pathlib import Path
import re,sys,tempfile,shutil,select,time,codecs
from test_board import Server
from test_telnet import Client

def run(binary,world):
    binary=str(Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='ln-clans-native-') as tmp:
        p=Path(tmp);game=p/'world';shutil.copytree(world,game)
        wrapper=p/'launch';shutil.copyfile(Path(__file__).resolve().parent.parent/'scripts/door_clans.sh',wrapper);wrapper.chmod(0o700)
        config=p/'doors';config.write_text(f'1|0|90|{wrapper}|{game}|The Clans|dorinfo1:1\n')
        s=Server(binary,p/'notes',members=p/'members',menu=True,telnet=True,language='en',options=['--doors',str(config)])
        a=Client(s.port)
        transcript=b''
        decoder=codecs.getincrementaldecoder('utf-8')()
        def collect():
            nonlocal transcript
            until=time.monotonic()+1.5
            while time.monotonic()<until:
                if select.select([a.sock],[],[],.2)[0]:a.receive()
                if b'\x1b[6n' in a.buf:
                    count=a.buf.count(b'\x1b[6n');a.buf=a.buf.replace(b'\x1b[6n',b'');a.send(b'\x1b[24;80R'*count)
            out=a.buf;a.buf=b'';transcript+=out
            decoder.decode(out) # Clans wrapper explicitly enables CP437UTF8.
            return out
        def intro():
            for _ in range(12):
                out=re.sub(rb'\x1b\[[0-?]*[ -/]*[@-~]',b'',collect())
                assert b'No dropfile' not in out and b'Back to BBS' not in out,out
                if b'Do you wish to join this game of clans?' in out:return
                if b'Enter option>' in out:a.send(b'e')
                else:a.send(b'\r') # The random ANSI title screens have several pause styles.
            raise AssertionError(('Clans did not reach new-player prompt',re.sub(rb'\x1b\[[0-?]*[ -/]*[@-~]',b'',transcript)[-1600:]))
        try:
            a.until();a.send(b'REGISTER alice\r\nclans-test-password\r\n');assert b'Logged in' in a.until()
            a.cmd('GAME',b'0:main] > ');a.send(b'1\r\n');intro()
            drops=list(game.glob('.liner-door-run-*/DORINFO1.DEF'));assert len(drops)==1
            assert drops[0].read_bytes().splitlines()[6]==b'LN000000001'
            # Declining a new game is a normal Clans exit, not an escape.
            a.send(b'n');collect();a.send(b'q');out=a.until(b'0:main] > ');transcript+=out
            assert b'Back to BBS' in out and b'1: The Clans' in out,out
            a.send(b'1\r\n');intro();a.send(b'\x1d')
            assert b'1: The Clans' in a.until(b'0:main] > ')
            time.sleep(.2)
            # Forced exit must not leave a stale Clans node blocking reentry.
            a.send(b'1\r\n');intro();a.send(b'n');collect();a.send(b'q');a.until(b'0:main] > ')
            assert b'MAIN' in a.cmd('0')
            assert b'PONG' in a.cmd('PING')
            a.cmd('GAME',b'0:main] > ');a.send(b'1\r\n');intro();a.close()
            time.sleep(.5)
            a=Client(s.port);a.until();a.send(b'REGISTER bob\r\nclans-test-password\r\n');assert b'Logged in' in a.until()
            a.cmd('GAME',b'0:main] > ');a.send(b'1\r\n');intro()
            drops=list(game.glob('.liner-door-run-*/DORINFO1.DEF'));assert len(drops)==1
            assert drops[0].read_bytes().splitlines()[6]==b'LN000000002'
            a.send(b'n');collect();a.send(b'q');a.until(b'0:main] > ')
            Path('clans-telnet.log').write_bytes(transcript)
        finally:Path('clans-telnet.log').write_bytes(transcript);a.close();s.close()
    print('PASS native Clans 0.97b2: two authenticated identities, UTF-8 ANSI, menu selection, intro keys, normal exit, escape, disconnect and reentry')

if __name__=='__main__':run(sys.argv[1],sys.argv[2])
