"""External UTF-8 menu files: literal display, startup validation and restart."""
import os
import pathlib
import subprocess
import sys
import tempfile
from test_board import Server
from test_ui import Menu
from test_telnet import Client as TelnetClient


def run(binary):
    binary = str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='menu-files-') as d:
        p = pathlib.Path(d); folder = p / 'custom menus'; folder.mkdir()
        ja, en = folder / 'ja.txt', folder / 'en.txt'
        ja.write_bytes('\ufeff外部メニュー 日本語\nV: 操作履歴\n100% %n %s\n'.encode())
        en.write_bytes(b'External menu\r\nV: History\r\nM: Menu\r\n')
        s = Server(binary, p/'data', menu=True, menu_dir=folder)
        a, b = Menu(s.port), Menu(s.port)
        try:
            assert '外部メニュー 日本語' in a.until()
            assert '外部メニュー 日本語' in b.until()
            assert '100% %n %s' in a.cmd('?')
            a.cmd('LANG en'); assert 'External menu' in a.cmd('MENU')
            assert '外部メニュー 日本語' in b.cmd('MENU')
            assert '管理者権限が必要' in b.cmd('AUDIT')
            en.write_bytes(b'Reloaded menu\n')
            assert 'External menu' in a.cmd('MENU')  # Startup snapshot, no partial live reload.
            a.cmd('RAW', 'RAW\r\n'); assert 'External menu' in a.cmd('MENU')
        finally:
            a.close(); b.close(); s.close()
        s = Server(binary, p/'data', menu=True, menu_dir=folder, language='en', telnet=True)
        a = TelnetClient(s.port)
        try:
            assert b'Reloaded menu\r\n' in a.until()
            a.cmd('LANG ja'); assert '外部メニュー'.encode() in a.cmd('MENU')
        finally:
            a.close(); s.close()
        print('PASS: UTF-8/BOM/LF/CRLF, literal percent signs, language isolation, RAW/MENU, Telnet and restart reload')

        good = ja.read_bytes()
        for content in [b'', b'\xef\xbb\xbf', b'x'*4097, b'bad\0text', b'\x1b[2J', b'\xff', b'\xc0\xaf', b'\xed\xa0\x80', b'\xf4\x90\x80\x80', b'\xe3\x81', b'\xc2\x85']:
            ja.write_bytes(content)
            untouched = p/'must-not-create'
            result = subprocess.run([binary, '--menu-dir', str(folder), '0', str(untouched)], capture_output=True, timeout=5)
            assert result.returncode and b'menus:' in result.stderr and not untouched.exists()
            assert ja.read_bytes() == content
        ja.write_bytes(good); en.unlink()
        result = subprocess.run([binary, '--menu-dir', str(folder), '0'], capture_output=True, timeout=5)
        assert result.returncode and b'menus:' in result.stderr
        print('PASS: invalid, empty, oversized and missing explicit files fail before data creation without modifying menus')

        old = os.getcwd()
        try:
            os.chdir(p)
            s = Server(binary, p/'data', menu=True)
            a = Menu(s.port)
            try:
                assert 'Liner notes' in a.until()  # No default folder: built-in fallback.
            finally:
                a.close(); s.close()
            default = p/'menus'; default.mkdir()
            (default/'ja.txt').write_bytes('既定メニュー\n'.encode())
            (default/'en.txt').write_bytes(b'Default external menu\n')
            s = Server(binary, p/'data', menu=True)
            a = Menu(s.port)
            try:
                assert '既定メニュー' in a.until()
                a.cmd('LANG en'); assert 'Default external menu' in a.cmd('MENU')
            finally:
                a.close(); s.close()
        finally:
            os.chdir(old)
        print('PASS: default menus directory and backwards-compatible fallback when absent')
        # Optional RTBBS sections use the selected language, are loaded once,
        # and fall back to compiled guidance when missing.
        en.write_bytes(b'Main external\n')
        section=folder/'settings-en.txt';section.write_bytes(b'Custom INSTALL 100%\n')
        s=Server(binary,p/'data',menu=True,menu_dir=folder,language='en');a=Menu(s.port)
        try:
            a.until();assert 'Custom INSTALL 100%' in a.cmd('I')
            section.write_bytes(b'Changed INSTALL\n');assert 'Custom INSTALL 100%' in a.cmd('?')
            a.cmd('LANG ja');assert '個人設定' in a.cmd('?')
            a.cmd('0');assert '外部メニュー' in a.cmd('MENU')
        finally:a.close();s.close()
        for content in [b'',b'x'*4097,b'\xff',b'\x1b[2J']:
            section.write_bytes(content);untouched=p/'invalid-section-data'
            result=subprocess.run([binary,'--menu-dir',str(folder),'0',str(untouched)],capture_output=True,timeout=5)
            assert result.returncode and not untouched.exists()
        print('PASS: optional RTBBS sections, language fallback, startup snapshot and validation')


if __name__ == '__main__':
    run(sys.argv[1])
