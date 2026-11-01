"""ZMODEM terminal gateway to the authenticated, protocol-neutral vault API.

The C BBS and its stores are accessed only through a normal member connection.
The ZMODEM process receives no BBS password, socket or store directory.
"""
import argparse
import getpass
import hashlib
import importlib.util
import os
import pathlib
import shutil
import signal
import sys
import tempfile

from transfer_connectors import LineConnector, download_begin, download_read, download_done, download_abort
from zmodem_connector import ZmodemProcess, TransferFailure, safe_name, MAX_BYTES

_spec = importlib.util.spec_from_file_location('zmodem_upload_workflow',
        pathlib.Path(__file__).with_name('vault-upload.py'))
_upload = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_upload)


class PublicationFailure(TransferFailure):
    def __init__(self, directory):
        super().__init__('Publication not confirmed; keep the recovery directory')
        self.directory = pathlib.Path(directory)


def check_member(connection):
    role = connection.protocol.request('ADMIN', lambda line: line.startswith(b'ROLE '))
    if role not in (b'ROLE member', b'ROLE admin'):
        raise TransferFailure('Active member login required')


def receive(connection, engine, host, port, user, spool_root, input_fd=0, output_fd=1,
            description=''):
    """One ZMODEM session -> private file -> normal resumable upload workflow.

    Nothing is reserved/published until the complete ZMODEM session succeeds.
    If publication is uncertain, retain source and the existing recovery state.
    """
    _upload.metadata('received.bin', description)
    def authorize():
        check_member(connection)
        if hasattr(connection,'check_upload'):connection.check_upload()
    authorize()
    work = pathlib.Path(tempfile.mkdtemp(prefix='liner-zmodem-', dir=spool_root))
    data = work / 'data'
    data.mkdir(mode=0o700)
    keep = False
    try:
        source = engine.receive(data, input_fd, output_fd, authorize)
        _upload.metadata(source.name, description)
        source.chmod(0o600)
        # Borrow the authenticated connection, preserving its revocation epoch.
        # Never reconnect automatically with an old password after suspension.
        class Borrowed:
            name = getattr(connection,'name','line-v1')
            def __new__(cls, *_):
                class Lease:
                    def __getattr__(self, key): return getattr(connection, key)
                    def close(self): pass
                return Lease()
        try:
            return _upload.upload(host, port, user, '', source, work / 'upload.json',
                                  description=description, category=getattr(connection,'upload_category',''), connector_factory=Borrowed, progress=getattr(connection,'registration_progress',None))
        except (OSError, ValueError) as error:
            keep = True
            raise PublicationFailure(work) from error
    finally:
        if not keep:
            shutil.rmtree(work)


def runtime_receive(connection,engine,host,port,user,spool_root,input_fd=0,output_fd=1,
                    description='',batch=False,resume=False):
    """One resumable transfer per endpoint/member/namespace/board; no credentials.

    Single-file interrupted wire transfers can resume. Batches are fully staged
    before publication; per-file publication receipts survive a later failure.
    A fresh upload archives an interrupted wire transfer instead of blocking all
    later ZUPLOADs. Uncertain publication still requires an explicit resume.
    """
    import fcntl,json,stat
    from zmodem_connector import staged_file,staged_files
    _upload.metadata('received.bin',description if description is not None else '')
    category=getattr(connection,'upload_category','')
    identity=[host,port,user,getattr(connection,'name','line-v1'),category]
    if getattr(connection,'transfer_protocol','z')!='z':identity.append(connection.transfer_protocol)
    key=hashlib.sha256(json.dumps(identity).encode()).hexdigest()
    work=pathlib.Path(spool_root)/('pending-'+key)
    def authorize():
        check_member(connection)
        if hasattr(connection,'check_upload'):connection.check_upload()
    authorize()
    fresh=False;lockfd=None
    # Serialize namespace changes only, not the transfer itself. Locking the
    # spool directory avoids persistent per-member lock files and unlink races.
    rootfd=os.open(spool_root,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
    try:
        fcntl.flock(rootfd,fcntl.LOCK_EX)
        try:work.mkdir(mode=0o700);fresh=True
        except FileExistsError:pass
        if work.is_symlink() or not work.is_dir() or work.stat().st_uid!=os.getuid() or work.stat().st_mode&0o077:
            raise TransferFailure('Invalid private resume directory')
        lockfd=os.open(work/'lock',os.O_RDWR|os.O_CREAT|os.O_NOFOLLOW,0o600)
        # Also respects older helpers which lock only the pending directory.
        fcntl.flock(lockfd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        if not fresh and not resume:
            meta=work/'transfer.json'
            if meta.is_symlink() or meta.stat().st_size>8192:raise TransferFailure('Invalid resume metadata')
            saved=json.loads(meta.read_text(encoding='utf-8'))
            if not isinstance(saved,dict) or saved.get('identity')!=identity:
                raise TransferFailure('Resume identity mismatch')
            if saved.get('received') is not False:
                # Never abandon receipts for data which may already be public.
                raise PublicationFailure(work)
            retained=pathlib.Path(tempfile.mkdtemp(prefix='retained-'+key+'-',dir=spool_root))
            os.rename(work,retained)
            _upload.sync_directory(pathlib.Path(spool_root))
            os.close(lockfd);lockfd=None
            work.mkdir(mode=0o700);fresh=True
            lockfd=os.open(work/'lock',os.O_RDWR|os.O_CREAT|os.O_NOFOLLOW,0o600)
            fcntl.flock(lockfd,fcntl.LOCK_EX|fcntl.LOCK_NB)
    except BaseException:
        if lockfd is not None:os.close(lockfd)
        raise
    finally:
        os.close(rootfd)
    keep=True
    try:
        meta=work/'transfer.json';data=work/'data'
        if fresh:
            if resume:keep=False;raise TransferFailure('No interrupted transfer', 'NO_PENDING')
            data.mkdir(mode=0o700)
            saved={'identity':identity,'description':description,'batch':batch,'received':False}
            meta.write_text(json.dumps(saved),encoding='utf-8');meta.chmod(0o600)
        else:
            if meta.is_symlink() or meta.stat().st_size>8192:raise TransferFailure('Invalid resume metadata')
            saved=json.loads(meta.read_text(encoding='utf-8'))
            if saved['identity']!=identity or (description is not None and saved['description']!=description) or saved['batch']!=batch:raise TransferFailure('Resume metadata mismatch', 'MISMATCH')
            description=saved['description']
            _upload.metadata('received.bin',description)
            batch=saved['batch']
        if data.is_symlink() or not data.is_dir():raise TransferFailure('Invalid staging directory')
        if not saved['received']:
            try:
                if batch:
                    sources=(engine.receive_many_resume if resume else engine.receive_many)(data,input_fd,output_fd,authorize)
                else:
                    source=(engine.receive_resume if resume else engine.receive)(data,input_fd,output_fd,authorize)
                    sources=[source]
            except (OSError,ValueError):
                # Retain valid partials only. A later receive must acknowledge
                # every retained file before the batch can be published.
                try:
                    partials=staged_files(data) if batch else [staged_file(data)]
                    keep=any(path is not None and path.stat().st_size>0 for path in partials)
                except (OSError,ValueError):keep=False
                if getattr(connection,'transfer_protocol','z')!='z':keep=False
                raise
            manifest={}
            for source in sources:
                with source.open('rb') as stream:
                    digest=hashlib.file_digest(stream,'sha256').hexdigest();os.fsync(stream.fileno())
                manifest[source.name]=[source.stat().st_size,digest]
            saved['manifest']=manifest;saved['received']=True
            temporary=work/'transfer.tmp';temporary.write_text(json.dumps(saved),encoding='utf-8')
            with temporary.open('rb') as file:os.fsync(file.fileno())
            os.replace(temporary,meta);_upload.sync_directory(work)
        sources=staged_files(data,required=True) if batch else [staged_file(data,required=True)]
        class Borrowed:
            name=getattr(connection,'name','line-v1')
            def __new__(cls,*_):
                class Lease:
                    def __getattr__(self,key):return getattr(connection,key)
                    def close(self):pass
                return Lease()
        if set(saved['manifest'])!={source.name for source in sources}:raise TransferFailure('Staged file set changed')
        for source in sources:
            with source.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
            if saved['manifest'][source.name]!=[source.stat().st_size,digest]:raise TransferFailure('Staged contents changed')
        receipts=[]
        for source in sources:
            authorize();state=work/(hashlib.sha256(source.name.encode()).hexdigest()+'.json')
            try:
                receipt=_upload.upload(host,port,user,'',source,state,resume=state.exists(),
                    description=description,category=category,connector_factory=Borrowed,progress=getattr(connection,'registration_progress',None))
                receipts.append(receipt)
            except (OSError,ValueError) as error:raise PublicationFailure(work) from error
        keep=False
        return (len(receipts),sum(r[1] for r in receipts),'') if batch else receipts[0]
    finally:
        if not keep:shutil.rmtree(work)
        os.close(lockfd)


def send(connection, engine, ident, spool_root, input_fd=0, output_fd=1, name=None):
    """Verify the entire immutable snapshot before exposing it to the terminal."""
    if not 1 <= ident <= 0x7ffffffe:
        raise TransferFailure('Invalid file ID')
    name = safe_name(name or f'file-{ident}.bin')
    check_member(connection)
    try:
        size, expected = download_begin(connection,ident)
        if not 0 <= size <= MAX_BYTES:
            raise TransferFailure('File exceeds 256 MiB')
        with tempfile.TemporaryDirectory(prefix='liner-zmodem-', dir=spool_root) as directory:
            path = pathlib.Path(directory) / name
            digest = hashlib.sha256()
            offset = 0
            with path.open('xb') as target:
                os.chmod(path, 0o600)
                while offset < size:
                    chunk = download_read(connection,ident,offset)
                    if not chunk or offset + len(chunk) > size:
                        raise TransferFailure('Invalid source chunk')
                    target.write(chunk)
                    digest.update(chunk)
                    offset += len(chunk)
                target.flush()
                os.fsync(target.fileno())
            if digest.hexdigest() != expected:
                raise TransferFailure('SHA-256 mismatch')
            def authorize():
                # Also detect withdrawal of the file, not only account revocation.
                if connection.info(ident) != (size, expected):
                    raise TransferFailure('File metadata changed')
            engine.send(path, input_fd, output_fd, authorize)
            download_done(connection,ident)
            return size, expected
    finally:
        download_abort(connection,ident)


MESSAGES = {
    'en': {
        'password': 'BBS password: ',
        'ready': 'Start your terminal ZMODEM transfer (one file).',
        'published': 'Published file #{0}, {1} bytes; SHA-256 {2}',
        'sent': 'ZMODEM send completed: {0} bytes; SHA-256 {1}',
        'failed': 'Transfer failed. No success is confirmed.',
        'recover': 'Publication is uncertain. Retained source/state: {0}',
        'tty': 'Use an SSH terminal or an 8-bit clean stdio transport.',
    },
    'ja': {
        'password': 'BBSパスワード: ',
        'ready': '端末のZMODEM送受信を開始してください（1ファイル）。',
        'published': 'ファイル #{0} を公開しました。{1} バイト; SHA-256 {2}',
        'sent': 'ZMODEM送信完了: {0} バイト; SHA-256 {1}',
        'failed': '転送に失敗しました。成功は確認されていません。',
        'recover': '公開結果を確認できませんでした。元ファイル・再開情報の保存先: {0}',
        'tty': 'SSH端末、または8ビット透過の標準入出力経路を使用してください。',
    },
}


def main():
    p = argparse.ArgumentParser(description='Prototype: ZMODEM terminal <-> Liner notes vault')
    p.add_argument('action', choices=['receive', 'send'])
    p.add_argument('--host', default='127.0.0.1')
    p.add_argument('--port', type=int, default=2222)
    p.add_argument('--user', required=True)
    p.add_argument('--id', type=int)
    p.add_argument('--name', help='Download filename; defaults to file-ID.bin')
    p.add_argument('--description', default='')
    p.add_argument('--lang', choices=MESSAGES, default='ja')
    p.add_argument('--spool-root', default=None)
    p.add_argument('--receiver', default='lrz')
    p.add_argument('--sender', default='lsz')
    p.add_argument('--timeout', type=int, default=120)
    p.add_argument('--password-fd', type=int, help='Automation: dedicated inherited FD >= 3')
    args = p.parse_args()
    if args.action == 'send' and args.id is None:
        p.error('send requires --id')
    if args.action == 'receive' and (args.id is not None or args.name is not None):
        p.error('--id and --name are download options')
    msg = MESSAGES[args.lang]
    if args.password_fd is not None and args.password_fd < 3:
        p.error('password FD must be >= 3; stdin/stdout carry ZMODEM')
    connection = None
    old_handlers = {}
    def interrupted(signum, frame):
        raise TransferFailure('Transfer interrupted')
    try:
        for sig in [getattr(signal, key) for key in ('SIGINT', 'SIGTERM', 'SIGHUP') if hasattr(signal, key)]:
            old_handlers[sig] = signal.signal(sig, interrupted)
        engine = ZmodemProcess(args.receiver, args.sender, args.timeout)
        engine.executable(args.receiver if args.action == 'receive' else args.sender)
        if args.password_fd is not None:
            with os.fdopen(args.password_fd, 'r', encoding='utf-8') as secret:
                password = secret.readline(512).rstrip('\r\n')
        else:
            if not os.isatty(0):
                raise TransferFailure(msg['tty'])
            password = getpass.getpass(msg['password'])
        connection = LineConnector(args.host, args.port, args.user, password)
        password = None
        connection.protocol.sock.settimeout(3)
        print(msg['ready'], file=sys.stderr, flush=True)
        if args.action == 'receive':
            result = receive(connection, engine, args.host, args.port, args.user,
                             args.spool_root, description=args.description)
            print(msg['published'].format(*result), file=sys.stderr)
        else:
            result = send(connection, engine, args.id, args.spool_root, name=args.name)
            print(msg['sent'].format(*result), file=sys.stderr)
        return 0
    except PublicationFailure as error:
        print(msg['recover'].format(error.directory), file=sys.stderr)
        return 2
    except (OSError, ValueError) as error:
        print(msg['failed'], file=sys.stderr)
        # Exception text can contain server-supplied bytes. Do not print it raw
        # into a user's terminal, and never expose engine-provided diagnostics.
        print(ascii(str(error)), file=sys.stderr)
        return 1
    finally:
        if connection is not None:
            connection.close()
        for sig, handler in old_handlers.items():
            signal.signal(sig, handler)


if __name__ == '__main__':
    raise SystemExit(main())
