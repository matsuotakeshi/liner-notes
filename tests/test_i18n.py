"""Catalog contracts and language isolation through actual sockets."""
import json
import pathlib
import re
import subprocess
import sys
import tempfile
from test_board import Server, post, read
from test_ui import Menu
from test_members import PASSWORD

ROOT=pathlib.Path(__file__).resolve().parents[1]


def catalogs():
    data={}
    for file in (ROOT/'src/locales').glob('*.def'):
        rows={}
        for line in file.read_text(encoding='utf-8').splitlines():
            if line.startswith('/*'): continue
            m=re.fullmatch(r'MESSAGE\((\w+), (".*")\)',line)
            assert m,(file,line)
            key,text=m[1],json.loads(m[2])
            assert key not in rows,(file,key,'duplicate')
            assert text,(file,key,'empty translation')
            rows[key]=text
        data[file.stem]=rows
    base=data['en']
    fmt=re.compile(r'%(?:[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?[hljztL]*[diouxXeEfFgGaAcsp]|%)')
    for lang,rows in data.items():
        assert rows.keys()==base.keys(),(lang,'missing or extra message IDs')
        for key,text in rows.items():
            assert fmt.findall(text)==fmt.findall(base[key]),(lang,key,'printf signature')
            assert '%' not in fmt.sub('',text),(lang,key,'unsupported format')
    # Every existing user-visible error emitted by the runtime/worker must have a mapping.
    ui=(ROOT/'src/ui.c').read_text(encoding='utf-8')
    for name in ['runtime.c','session.c','board.c']:
        source=(ROOT/'src'/name).read_text(encoding='utf-8')
        errors=set(re.findall(r'"(ERR [^"\\]+)',source))
        assert all('"'+error+'"' in ui for error in errors),(name,errors)
    print('PASS: complete UTF-8 catalogs, unique IDs, printf signatures and all protocol error mappings')


def run(binary):
    catalogs(); binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='i18n-') as d:
        p=pathlib.Path(d); data=p/'data'; users=p/'users'; config=p/'boards'
        config.write_text('1|0|all|member|談話室\n2|1|all|none|Announcements\n',encoding='utf-8')
        s=Server(binary,data,members=users,catalog=config,menu=True,language='en')
        a,b=Menu(s.port),Menu(s.port)
        try:
            assert 'Select board' in a.until(); assert 'Guest | Board' in b.until()
            assert '日本語' in b.cmd('LANG ja')
            assert 'ゲスト | ボード' in b.cmd('LANG')
            assert 'Unknown command' in a.cmd('bad-command')
            assert '操作が分かりません' in b.cmd('bad-command')
            assert 'Unsupported language' in a.cmd('LANG fr')
            assert 'Display language: en' in a.cmd('LANG')
            assert 'Username' in a.cmd('REGISTER','> ')
            assert 'Password' in a.cmd('alice','> ')
            assert 'Logged in as alice' in a.cmd(PASSWORD)
            assert '談話室' in a.cmd('B'); selected=a.cmd('1'); assert 'Base notes' in selected,repr(selected)
            assert 'New note title' in a.cmd('POST','> ')
            assert 'Enter the body' in a.cmd('日本語の題名','Body > ')
            a.cmd('LANG ja','Body > '); a.cmd('本文は翻訳しない','Body > ')
            assert 'Saved article #1' in a.cmd('.')
            before=a.cmd('1'); assert 'Article #1' in before and 'LANG ja\r\n本文は翻訳しない' in before
            assert 'ボード 1 談話室 | 記事 1' in a.cmd('LANG ja')
            after=a.cmd('1'); assert '記事 #1' in after and 'LANG ja\r\n本文は翻訳しない' in after
            a.cmd('LANG en'); assert 'Parent #0' in a.cmd('1')
            assert 'Reply title' in a.cmd('REPLY','> ')
            a.cmd('English reply','Body > '); a.cmd('Unchanged body','Body > ')
            assert 'Saved article #2' in a.cmd('.')
            assert 'Article #1' in a.cmd('PARENT')
            assert 'Reply tree' in a.cmd('TREE')
            assert 'Article #2' in a.cmd('CHILD')
            assert 'No article in that direction' in a.cmd('NEXT')
            assert 'Marked article #2 as read' in a.cmd('SEEN')
            response=a.cmd('SEEN NEXT'); assert 'Article #1' in response
            assert 'No unread articles' in a.cmd('SEEN NEXT')
            a.cmd('B'); a.cmd('2'); a.cmd('POST','> ')
            assert 'cannot post' in a.cmd('Forbidden')
            assert 'Please log in' in b.cmd('LANG en')+b.cmd('POST')
            b.cmd('LANG ja'); assert '先にL' in b.cmd('POST')
            a.cmd('B'); a.cmd('1'); a.cmd('RAW','RAW\r\n')
            for i in range(8): post(a,f'page-{i}','body')
            assert read(a,1)[2]=='LANG ja\r\n本文は翻訳しない\r\n'
            assert 'Select board' in a.cmd('MENU')
            assert '+: Next page' in a.cmd('NOTES')
            a.cmd('LANG ja'); assert 'page-4' in a.cmd('+')
            assert '次のページはありません' in b.cmd('+')
            a.cmd('LANG en'); assert 'Logged out' in a.cmd('LOGOUT')
            assert 'Display language: en' in a.cmd('LANG')
            a.close(); a=Menu(s.port); assert 'Guest | Board' in a.until()
            assert 'ゲスト | ボード' in b.cmd('LANG')
            print('PASS: per-session languages, default, invalid locale, reconnect and RAW/MENU persistence')
            print('PASS: English registration/post/reply/navigation/unread/ACL; language switch preserves current article and page')
            print('PASS: UTF-8 titles/body unchanged; LANG inside editor is stored as text')
        finally: a.close(); b.close(); s.close()
        for args in [['--lang','xx','0'],['--lang']]:
            result=subprocess.run([binary]+args,capture_output=True,timeout=5)
            assert result.returncode==2 and b'invalid option' in result.stderr
        print('PASS: unsupported or missing startup language rejected')


if __name__=='__main__': run(sys.argv[1])
