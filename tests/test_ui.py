"""Real socket checks of Japanese menus, with unchanged raw protocol clients."""
import pathlib
import sys
import tempfile
import time
import os
import signal
from test_runtime import Client
from test_board import Server, post
from test_members import auth, PASSWORD


class Menu(Client):
    def until(self, ending='] > '):
        marker=ending.encode()
        self.sock.settimeout(10)
        while marker not in self.buf:
            data=self.sock.recv(8192)
            assert data, self.buf
            self.buf+=data
        end=self.buf.index(marker)+len(marker)
        result,self.buf=self.buf[:end],self.buf[end:]
        return result.decode('utf-8')

    def cmd(self, text, ending='] > '):
        self.send((text+'\n').encode()); return self.until(ending)


def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='menu-') as d:
        p=pathlib.Path(d); board=p/'board'; members=p/'members'; config=p/'boards'
        config.write_text('1|0|all|member|談話室\n2|1|all|none|お知らせ\n3|1|member|member|会員室\n',encoding='utf-8')
        s=Server(binary,board,members=members,catalog=config,menu=True)
        a=Menu(s.port); g=Menu(s.port)
        try:
            assert 'Liner notes' in a.until(); g.until()
            assert 'ログイン' in g.cmd('SEEN NEXT')
            a.cmd('REGISTER','> '); a.cmd('alice','> ')
            response=a.cmd(PASSWORD)
            assert 'ログインしました' in response and PASSWORD not in response
            assert '談話室' in a.cmd('B')
            assert '表示する項目はありません' in a.cmd('1')
            a.cmd('POST','> ')
            assert '取り消しました' in a.cmd('/CANCEL')
            a.cmd('POST','> '); assert '本文を入力' in a.cmd('最初のノート','本文 > ')
            for body in ['こんにちは','END','MENU']:
                a.cmd(body,'本文 > ')
            assert '記事 #1 を保存' in a.cmd('.')
            response=a.cmd('1'); assert 'こんにちは\r\nEND\r\nMENU\r\n' in response
            a.cmd('REPLY','> '); a.cmd('返信です','本文 > '); a.cmd('返信の本文','本文 > ')
            assert '記事 #2 を保存' in a.cmd('.')
            assert '記事 #1 ' in a.cmd('PARENT')
            assert '記事 #2 ' in a.cmd('CHILD')
            assert '返信ツリー' in a.cmd('TREE')
            assert '返信です' in a.cmd('2')
            assert 'その方向' in a.cmd('NEXT')
            assert '記事 2]' in a.cmd('ACCOUNT')
            a.cmd('1')
            response=a.cmd('SEEN NEXT'); assert '既読にしました' in response and '記事 #2 ' in response
            assert '未読記事はありません' in a.cmd('SEEN NEXT')
            assert '表示する項目はありません' in a.cmd('UNREAD')
            a.cmd('B'); a.cmd('2'); a.cmd('POST','> ')
            assert '投稿できません' in a.cmd('拒否される題名')
            g.cmd('B'); assert '選べません' in g.cmd('3')
            assert 'ボード 1' in g.cmd('?')
            a.cmd('B'); a.cmd('1')
            a.cmd('RAW','RAW\r\n')
            a.send(b'PING\n'); a.expect(b'PONG')
            for i in range(30): post(a,f'note-{i}','body')
            assert 'Liner notes' in a.cmd('MENU')
            first=a.cmd('NOTES'); assert '+: 次ページ' in first
            second=a.cmd('+'); assert second!=first and 'ベースノート一覧' in second
            assert '次のページはありません' in g.cmd('+')
            a.cmd('POST','> '); a.cmd('取消','本文 > ')
            assert '取り消しました' in a.cmd('/CANCEL')
            assert 'ログアウトしました' in a.cmd('LOGOUT')
            time.sleep(1.1)
            a.cmd('LOGIN','> '); a.cmd('alice','> ')
            assert '取り消しました' in a.cmd('/CANCEL')
            a.cmd('LOGIN','> '); a.cmd('alice','> '); assert 'ログインしました' in a.cmd(PASSWORD)
            a.cmd('Q','> '); a.send(b'Y\n'); assert 'ありがとうございました' in a.until('\r\nご利用ありがとうございました。\r\n')
            assert not a.sock.recv(1)
            print('PASS: guided registration/login, UTF-8 post/reply, cancel, navigation and saved-then-next unread')
            print('PASS: board selection, denied access/post, independent pagination, raw/menu switching and quit')
        finally: a.close(); g.close(); s.close()
        s=Server(binary,board,members=members,catalog=config)
        a=Menu(s.port)
        try:
            a.send(b'PING\n'); a.expect(b'PONG')
            assert 'Liner notes' in a.cmd('MENU')
        finally: a.close(); s.close()
        print('PASS: opt-in MENU works on default raw server after restart')
        if os.name=='posix':
            import fault_io as resource
            # This fault targets the member read-state journal. Stage60 access
            # logging has its own short-write tests and must not fail first.
            fault_admins=p/'fault-admins';fault_admins.write_text('1\n')
            s=Server(binary,board,members=members,catalog=config,admins=fault_admins);a=Client(s.port)
            try:
                auth(a,'alice');a.send(b'BOARDOPTIONS 1 BOARD 1 0 0 0\n');a.expect(b'BOARD OPTIONS SAVED')
            finally:a.close();s.close()
            limit=members.stat().st_size+64
            def size_limit():
                signal.signal(signal.SIGXFSZ,signal.SIG_IGN)
                resource.setrlimit(resource.RLIMIT_FSIZE,(limit,limit))
            s=Server(binary,board,members=members,catalog=config,menu=True,preexec_fn=size_limit,options=["--user-log","-"])
            a=Menu(s.port)
            try:
                a.until(); a.cmd('LOGIN alice','> '); a.cmd(PASSWORD)
                a.cmd('3')
                reply=a.cmd('SEEN NEXT')
                assert '未読巡回は進めていません' in reply and '記事 3]' in reply
                assert '記事 #3 ' not in reply
                assert '会員記録を利用できません' in a.cmd('NEW')
            finally: a.close(); s.close()
            assert members.stat().st_size==limit
            print('PASS: real short-write on seen journal stops menu traversal and preserves unread article')


if __name__=='__main__': run(sys.argv[1])
