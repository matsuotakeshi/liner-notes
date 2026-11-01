"""Run only against serve_external_check.py's temporary test server."""
import sys
from test_telnet import Client
c=Client(int(sys.argv[2]),host=sys.argv[1])
try:
    assert b'Liner notes' in c.until()
    c.cmd('REGISTER lanprobe',b'Password (8-128 bytes)> ')
    reply=c.cmd('LAN-test-secret-2468')
    assert b'Logged in as lanprobe' in reply and b'LAN-test-secret' not in reply
    c.cmd('POST',b'title (up to 80 bytes)> ')
    c.cmd('LAN test article',b'Body > ')
    c.cmd('Posted from another machine',b'Body > ')
    assert b'Saved article #1' in c.cmd('.')
    assert b'Posted from another machine' in c.cmd('1')
    assert b'Goodbye' in c.cmd('Q',b'Goodbye.')
finally:c.close()
print('PASS: actual cross-host Telnet registration, hidden password, post/read and quit')
