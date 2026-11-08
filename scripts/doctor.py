#!/usr/bin/env python3
"""Read-only local installation diagnostics. Never sources configuration or runs games."""
from __future__ import annotations
import sys
sys.dont_write_bytecode = True
if sys.version_info < (3,11):
    print('liner-notes-doctor: Python 3.11以降が必要です / Python 3.11 or newer is required.',file=sys.stderr)
    raise SystemExit(2)
import argparse
import datetime
import hashlib
import importlib.util
import ipaddress
import json
import os
from pathlib import Path, PurePosixPath
from contextlib import closing, contextmanager
import platform
import re
import shlex
import shutil
import sqlite3
import ssl
import stat
import subprocess
import tempfile
import time
import tomllib
from urllib.parse import urlsplit
from zoneinfo import ZoneInfo

# No BBS/Web/link modules are imported: several initialise stores on construction.
CHECKS = ('config', 'database', 'links', 'doors', 'transfers', 'permissions', 'selinux')
TEXT = {
 'ok': ('確認できました。', 'Check passed.'),
 'missing': ('必要なファイルまたはディレクトリがありません。', 'A required file or directory is missing.'),
 'unreadable': ('読み取りできません。', 'The diagnostic account cannot read this item.'),
 'syntax': ('設定の構文または値が不正です。', 'Configuration syntax or value is invalid.'),
 'dynamic': ('動的なシェル設定のため静的に確定できません。実行はしていません。', 'Dynamic shell configuration cannot be resolved statically; it was not executed.'),
 'disabled': ('未使用のオプション機能です。', 'This optional feature is not configured/enabled.'),
 'permission': ('サービス用アカウントのアクセス権が不足、または保護が不適切です。', 'Service-account access is insufficient, or private data is exposed.'),
 'unsafe': ('安全な通常ファイル/ディレクトリではありません。', 'This is not a safe regular file/directory.'),
 'account': ('サービス用アカウントを確認できません。', 'The service account could not be resolved.'),
 'library': ('必要なライブラリ/補助プログラムを確認できません。', 'A required library/helper could not be found.'),
 'notrunning': ('停止中、または起動状態を取得できません。', 'The service is stopped or its state is unavailable.'),
 'mismatch': ('設定と実際の起動プログラム/接続先が一致しません。', 'Configuration differs from the running program or endpoint.'),
 'database': ('SQLiteの整合性または保存形式を確認できません。', 'SQLite integrity or storage schema could not be verified.'),
 'busy': ('保存領域が変化中です。整合性の断定を保留しました。', 'The store changed during inspection; integrity is inconclusive.'),
 'limit': ('診断のコピー容量/時間上限を超えます。', 'The diagnostic snapshot size/time limit was reached.'),
 'space': ('空き容量が不足しています。', 'Free disk space is low.'),
 'secret': ('秘密ファイルの形式・所有権・保護を確認してください。内容は表示しません。', 'Check secret-file format, owner and protection; its contents are never displayed.'),
 'certificate': ('鍵と証明書の形式・対応・有効期限が不正です。', 'Key/certificate format, correspondence or validity period is invalid.'),
 'resume': ('再開検査の記録がないか、受信プログラムのハッシュと一致しません。', 'The resume receipt is missing or does not match the receiver binary.'),
 'selinux': ('SELinuxの制約またはラベルが期待値と一致しません。', 'SELinux enforcement, domain or label differs from the expected policy.'),
 'unconfined': ('専用の制約ドメインでは動作していません。', 'The process is not running in the dedicated confined domain.'),
 'unsupported': ('この環境の検査は未対応/限定的です。', 'Inspection on this environment is unsupported or limited.'),
 'limited': ('静的な検査のみです。実行・通信・権限変更はしていません。', 'Only static inspection was performed; no execution, connection or permission change.'),
}
FIX = {
 'ok': ('変更は不要です。', 'No change is required.'),
 'disabled': ('使用する場合だけ設定してから再診断してください。', 'Configure this feature only if needed, then run the check again.'),
 'busy': ('停止済みの整合バックアップ、または更新のない時間に再診断してください。', 'Retry against a quiescent store or a consistent offline backup.'),
 'limit': ('空き容量を確認し --max-database-bytes / --database-timeout を調整してください。', 'Check free space and adjust --max-database-bytes / --database-timeout.'),
 'resume': ('管理者が検証済みlrzszで liner-notes-check-resume を実行してください。doctor自身は転送しません。', 'Run liner-notes-check-resume with the verified lrzsz; doctor itself never transfers data.'),
 'selinux': ('専用ポリシーとsemanageのローカル設定を確認し、必要な対象だけラベルを復旧してください。無効化はしないでください。', 'Review the dedicated policy and local semanage mappings; restore only intended labels. Do not disable SELinux.'),
 'unconfined': ('専用ポリシーのエントリポイントとsystemdの設定を確認してください。', 'Review dedicated-policy entrypoints and systemd settings.'),
 'secret': ('サービスの所有者と0600権限、形式を管理者が確認してください。', 'Have an administrator verify service ownership, mode 0600 and file format.'),
 'certificate': ('同じ識別鍵に対応する有効なEd25519証明書と信頼先を設定してください。', 'Configure valid Ed25519 identity certificates and matching pinned peers.'),
 'space': ('既存データを保全したうえで容量を確保してください。', 'Preserve existing data and provide more disk space.'),
 'dynamic': ('設定を実行せず確認し、--program / --data-dir / --database 等で明示してください。', 'Inspect configuration without executing it; use explicit --program / --data-dir / --database overrides.'),
 'default': ('統合マニュアルの該当設定・所有者・依存関係を確認し、修正後に再診断してください。', 'Review the corresponding manual settings, ownership and dependencies, then rerun the check.'),
}
FLAGS = {'--menu', '--telnet', '--rtbbs-member-policy', '--userlog-public'}
VALUES = set('database database-structured limits timezone sysinfo goodbye doors link-accounts zmodem-helper vault-dir menu-dir help-dir admins lang bind banner disable registration id-mode id-prefix id-digits id-first login-text-dir registration-form max-articles max-mail max-user-posts gather-count user-log chat-log userlog-keep idle-seconds level-seconds time-warning time-grace'.split())


class InspectionChanged(ValueError):
    """An inspected pathname changed before a safe descriptor was obtained."""


def fingerprint(st):
    # Windows lstat/fstat expose different ctime meanings on some Python builds.
    # All supported UNIX targets retain the change-time check.
    return (st.st_dev,st.st_ino,st.st_size,st.st_mtime_ns,st.st_ctime_ns if os.name!='nt' else 0)


@contextmanager
def open_regular(path,maximum=None,private=False,expected=None):
    """No-follow, nonblocking read; verify the opened object before reading."""
    before=path.lstat()
    def validate(st):
        if not stat.S_ISREG(st.st_mode) or (maximum is not None and st.st_size>maximum) or (private and (st.st_nlink!=1 or st.st_mode&0o077)):
            raise ValueError('unsafe/large file')
    validate(before)
    if expected is not None and fingerprint(before)!=expected:
        raise InspectionChanged('source changed')
    flags=os.O_RDONLY|getattr(os,'O_NOFOLLOW',0)|getattr(os,'O_NONBLOCK',0)|getattr(os,'O_CLOEXEC',0)
    fd=os.open(path,flags)
    try:
        after=os.fstat(fd);validate(after)
        if fingerprint(before)!=fingerprint(after):
            raise InspectionChanged('source changed')
        with os.fdopen(fd,'rb',closefd=False) as stream:
            yield stream
    finally:os.close(fd)


def assignments(text):
    """Read conservative rc.conf/EnvironmentFile assignments without evaluation."""
    values, dynamic = {}, False
    for raw in text.replace('\\\n', '').splitlines():
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        match = re.fullmatch(r'(?:export\s+)?([A-Za-z_][A-Za-z_0-9]*)\s*=\s*(.*)', line)
        if not match:
            dynamic = True
            continue
        name, value = match.groups()
        if re.search(r'[`$;]|\x00', value):
            dynamic = True
            continue
        tokens = shlex.split(value, comments=True, posix=True)
        if len(tokens) > 1:
            raise ValueError('assignment')
        values[name] = tokens[0] if tokens else ''
    return values, dynamic


def options(text):
    args = shlex.split(text, posix=True)
    out = {}
    while args and args[0].startswith('--'):
        key = args.pop(0)
        if key in FLAGS:
            out[key[2:]] = True
        elif key[2:] in VALUES and args:
            value = args.pop(0)
            if value.startswith('--'):
                raise ValueError('missing option value')
            if key in {'--database','--database-structured'}:
                out.pop('database' if key=='--database-structured' else 'database-structured',None)
            out[key[2:]] = value
        else:
            raise ValueError('unknown option')
    if not 1 <= len(args) <= 4 or not args[0].isdigit() or not 1 <= int(args[0]) <= 65535:
        raise ValueError('port/paths')
    out['port'] = int(args[0])
    out.update(zip(('board', 'members', 'catalog'), args[1:]))
    ipaddress.IPv4Address(out.get('bind', '127.0.0.1'))
    for key, allowed in [('lang', {'ja', 'en'}), ('registration', {'open', 'approval', 'closed'}), ('id-mode', {'free', 'auto'}), ('registration-form', {'simple', 'detailed'})]:
        if key in out and out[key] not in allowed:
            raise ValueError('enum')
    for key,maximum in [('max-articles',2147483646),('max-mail',2147483646),('max-user-posts',2147483646),('idle-seconds',86400),('userlog-keep',1000000),('time-warning',1000000),('time-grace',1000000),('gather-count',1000)]:
        if key in out and (not out[key].isascii() or not out[key].isdigit() or int(out[key]) > maximum):
            raise ValueError('number')
    if 'id-digits' in out and (len(out['id-digits'])!=1 or out['id-digits'] not in '12345678'):
        raise ValueError('id digits')
    if 'disable' in out and out['disable'] not in {'chat','mail','vault'}:
        raise ValueError('feature')
    if 'level-seconds' in out:
        parts=out['level-seconds'].split(':')
        if len(parts)!=2 or not all(p.isascii() and p.isdigit() for p in parts) or int(parts[0])>5 or int(parts[1])>86400:
            raise ValueError('level duration')
    return out


class Doctor:
    def __init__(self, args):
        self.args = args
        self.root = Path(args.root).resolve()
        self.staged = self.root != Path('/').resolve()
        self.system = args.os or platform.system()
        self.prefix = args.prefix or {'FreeBSD':'/usr/local','SunOS':'/opt/liner-notes'}.get(self.system,'/usr')
        self.config_dir = args.config_dir or (self.prefix + '/etc/liner-notes' if self.system == 'FreeBSD' else '/etc/liner-notes')
        self.data_dir = args.data_dir or {'FreeBSD':'/var/db/liner-notes','SunOS':'/var/opt/liner-notes'}.get(self.system,'/var/lib/liner-notes')
        self.user = args.service_user or 'liner_notes'
        self.program = args.program or self.prefix + '/sbin/liner-notes'
        self.core, self.env, self.results, self.pid, self.service_program = {}, {}, [], {}, {}
        self.enabled = set(args.enable or [])
        self.selected = set(CHECKS if args.check_all else [x for x in CHECKS if getattr(args, 'check_' + x)])
        self.language = args.lang

    def add(self, group, code, status='ok', location='', detail=''):
        index = 0 if self.language == 'ja' else 1
        # Only caller-defined details enter output, never exception text or source lines.
        safe = lambda value: re.sub(r'[\x00-\x1f\x7f]', '?', str(value))[:2048]
        self.results.append(dict(check=group, code=code, status=status, location=safe(location),
                                 message=TEXT[code][index], remedy=FIX.get(code, FIX['default'])[index], detail=safe(detail)))

    def path(self, value, relative=None):
        # Paths describe the target UNIX filesystem, even in Windows fixture tests.
        path = PurePosixPath(str(value).replace('\\', '/'))
        if not path.is_absolute():
            path = PurePosixPath(relative or self.data_dir) / path
        if self.staged:
            path = self.root / str(path).lstrip('/')
            if not path.resolve().is_relative_to(self.root):
                raise ValueError('outside inspection root')
        return Path(path)

    def read(self, value, maximum=1048576, relative=None, secret=False):
        path = self.path(value, relative)
        with open_regular(path,maximum,secret) as stream:
            data = stream.read(maximum + 1)
        if len(data) > maximum:
            raise ValueError('large file')
        return data

    def account(self, name):
        if self.staged:
            try:
                primary=None
                for line in self.read('/etc/passwd').decode().splitlines():
                    parts = line.split(':')
                    if parts[0] == name and len(parts) >= 4:
                        primary=(int(parts[2]),{int(parts[3])})
                        break
                if primary is not None:
                    try:
                        for line in self.read('/etc/group').decode().splitlines():
                            parts=line.split(':')
                            if len(parts)>=4 and name in parts[3].split(','):
                                primary[1].add(int(parts[2]))
                    except FileNotFoundError:pass
                    return primary
            except (OSError, ValueError, UnicodeError):
                pass
            return None
        try:
            import pwd
            entry = pwd.getpwnam(name)
            return entry.pw_uid, set(os.getgrouplist(name, entry.pw_gid))
        except (ImportError, KeyError, OSError):
            return None

    def access(self, path, user, bits):
        account = self.account(user)
        if account is None:
            return None
        uid, groups = account
        st = path.stat()
        mode = (st.st_mode >> 6) & 7 if st.st_uid == uid else (st.st_mode >> 3) & 7 if st.st_gid in groups else st.st_mode & 7
        if uid == 0:
            return bits & 1 == 0 or bool(st.st_mode & 0o111)
        if mode & bits != bits:
            return False
        # Ancestor traversal is part of access, not just the leaf's mode.
        for parent in path.parents:
            if self.staged and parent == self.root.parent:
                break
            ps = parent.stat()
            pm = (ps.st_mode >> 6) & 7 if ps.st_uid == uid else (ps.st_mode >> 3) & 7 if ps.st_gid in groups else ps.st_mode & 7
            if not pm & 1:
                return False
            if parent == self.root:
                break
        return True

    def item(self, group, value, user=None, directory=False, writable=False, executable=False, private=False, optional=False):
        try:
            path = self.path(value)
            st = path.lstat()
            if stat.S_ISLNK(st.st_mode) and executable and not private:
                path = path.resolve()
                if self.staged and not path.is_relative_to(self.root):
                    raise ValueError('outside inspection root')
                st = path.stat()
            if not (stat.S_ISDIR(st.st_mode) if directory else stat.S_ISREG(st.st_mode)):
                self.add(group, 'unsafe', 'error', value)
                return False
            if private and (st.st_mode & 0o077 or (not directory and st.st_nlink != 1)):
                self.add(group, 'permission', 'error', value)
                return False
            bits = (5 if directory or executable else 4) | (2 if writable else 0)
            allowed = self.access(path, user or self.user, bits)
            if allowed is False:
                self.add(group, 'permission', 'error', value)
                return False
            if allowed is None:
                self.add(group, 'account', 'warning', value)
            self.add(group, 'ok', location=value)
            return True
        except FileNotFoundError:
            self.add(group, 'missing', 'warning' if optional else 'error', value)
        except (PermissionError, OSError):
            self.add(group, 'unreadable', 'error', value)
        except ValueError:
            self.add(group, 'unsafe', 'error', value)
        return False

    def command(self, name, args):
        """Fixed system utilities only; never run executables supplied by config."""
        if self.staged:
            return None
        allowed = {'systemctl', 'rpm', 'dpkg-query', 'pkg', 'ps', 'getenforce', 'semodule', 'ls', 'matchpathcon', 'readelf', 'sockstat', 'netstat', 'procstat', 'ldconfig'}
        if name not in allowed:
            raise ValueError('not a read-only system query')
        for folder in ('/usr/bin', '/usr/sbin', '/bin', '/sbin', '/usr/local/bin', '/usr/local/sbin'):
            executable = Path(folder) / name
            if executable.is_file() and os.access(executable, os.X_OK):
                try:
                    r = subprocess.run([str(executable), *args], stdin=subprocess.DEVNULL, capture_output=True,
                                       timeout=8, env={'PATH':'/usr/sbin:/usr/bin:/sbin:/bin:/usr/local/bin', 'LC_ALL':'C'})
                    return (r.returncode, r.stdout.decode('utf-8', errors='replace')[:262144])
                except (OSError, subprocess.TimeoutExpired):
                    return None
        return None

    def load(self):
        config = self.args.config or self.config_dir + {'FreeBSD':'/liner_notes.conf','SunOS':'/solaris.json'}.get(self.system,'/liner-notes.env')
        if self.system=='SunOS':
            try:
                settings=json.loads(self.read(config).decode('utf-8'))
                arguments=settings['arguments']
                if not isinstance(arguments,list) or not all(isinstance(x,str) for x in arguments):
                    raise ValueError('arguments')
                if any(not isinstance(settings[k],str) or not PurePosixPath(settings[k]).is_absolute() for k in ('program','working_directory')):
                    raise ValueError('absolute paths required')
                self.core=options(shlex.join(arguments))
                self.program=self.args.program or settings['program']
                self.data_dir=self.args.data_dir or settings['working_directory']
            except (OSError,KeyError,TypeError,ValueError):
                self.add('config','syntax','error',config)
            files=[]
        else:
            files = [config]
        if self.system == 'FreeBSD' and not self.args.no_rc_conf:
            files += ['/etc/rc.conf', '/etc/rc.conf.local']
        for number, name in enumerate(files):
            try:
                values, dynamic = assignments(self.read(name).decode('utf-8'))
                self.env.update(values)
                if dynamic:
                    self.add('config', 'dynamic', 'warning', name)
            except FileNotFoundError:
                if number == 0:
                    self.add('config', 'missing', 'error', name)
            except (ValueError, UnicodeError):
                self.add('config', 'syntax', 'error', name)
            except OSError:
                self.add('config', 'unreadable', 'error', name)
        if self.system == 'FreeBSD':
            self.core = {k[len('liner_notes_'):].replace('_', '-'):v for k,v in self.env.items() if k.startswith('liner_notes_')}
            self.program = self.args.program or self.env.get('liner_notes_program', self.program)
            self.data_dir = self.args.data_dir or self.env.get('liner_notes_data_dir', self.data_dir)
            self.user = self.args.service_user or self.env.get('liner_notes_run_user', self.user)
            self.core['lang'] = self.core.get('language', 'ja')
            try:
                self.core['port'] = int(self.core.get('port', 2222))
                if not 1 <= self.core['port'] <= 65535:
                    raise ValueError('port')
                ipaddress.IPv4Address(self.core.get('bind', '127.0.0.1'))
                candidate=[]
                for key in VALUES:
                    if key in self.core and self.core[key]!='':
                        candidate.extend(['--'+key,str(self.core[key])])
                candidate.append(str(self.core['port']))
                options(shlex.join(candidate))
                if self.core.get('database-mode','blocks') not in {'blocks','structured'}:
                    raise ValueError('database mode')
                for key in ('chat','mail','vault','userlog-public','rtbbs-member-policy'):
                    if self.core.get(key,'NO') not in {'YES','NO'}:
                        raise ValueError('boolean')
            except ValueError:
                self.add('config', 'syntax', 'error', config)
        elif 'LINER_NOTES_OPTIONS' in self.env:
            try:
                self.core = options(self.env['LINER_NOTES_OPTIONS'])
            except ValueError:
                self.add('config', 'syntax', 'error', config)
        elif self.system!='SunOS':
            self.add('config', 'syntax', 'error', config)
        if self.args.database:
            self.core.pop('database-structured',None)
            self.core['database'] = self.args.database
        if self.args.doors_config:
            self.core['doors'] = self.args.doors_config
        if self.core.get('doors'):
            self.enabled.add('doors')
        if self.core.get('zmodem-helper'):
            self.enabled.add('transfers')
        if self.core.get('link-accounts') or self.args.link_config:
            self.enabled.add('links')
        if self.args.web_config:
            self.enabled.add('web')
        for feature, unit in [('core','liner-notes'), ('web','liner-notes-web'), ('links','liner-notes-link')]:
            result = self.command('systemctl', ['show', unit+'.service', '--property=MainPID,ActiveState,UnitFileState,ExecStart,User,WorkingDirectory', '--no-pager'])
            if result and result[0] == 0:
                values = dict(line.split('=', 1) for line in result[1].splitlines() if '=' in line)
                if values.get('ActiveState') == 'active' or values.get('UnitFileState') in {'enabled','enabled-runtime'}:
                    self.enabled.add(feature)
                if values.get('MainPID', '').isdigit() and int(values['MainPID']) > 0:
                    self.pid[feature] = int(values['MainPID'])
                if feature == 'core':
                    if values.get('User') and values['User']!=self.user:
                        self.add('config','mismatch','warning',detail='service User differs; use --service-user for an intentional override')
                    if values.get('WorkingDirectory') and values['WorkingDirectory']!=self.data_dir:
                        self.add('config','mismatch','warning',detail='service WorkingDirectory differs; use --data-dir for an intentional override')
                    match = re.search(r'path=([^ ;}]+)', values.get('ExecStart',''))
                    if match:
                        self.service_program[feature] = match[1]
        if self.system == 'FreeBSD':
            for feature,variable in [('web','liner_notes_web_enable'),('links','liner_notes_link_enable')]:
                if self.env.get(variable,'NO').upper() in {'YES','TRUE','ON','1'}:
                    self.enabled.add(feature)
            pidfile = self.core.get('child-pidfile', '/var/run/liner-notes.child.pid')
            try:
                value = self.read(pidfile, 40).decode().strip()
                if value.isdigit() and int(value) > 1:
                    self.pid['core'] = int(value)
            except (OSError, ValueError, UnicodeError):
                pass

    def check_config(self):
        machine = 'staged filesystem (target CPU unverified)' if self.staged else platform.machine()
        self.add('config', 'ok', detail=f'{self.system} / {machine} / Python {platform.python_version()} / SQLite {sqlite3.sqlite_version} / {ssl.OPENSSL_VERSION}')
        if self.system not in {'FreeBSD', 'Linux', 'SunOS'}:
            self.add('config', 'unsupported', 'warning')
        self.item('config', self.program, executable=True)
        try:
            binary_data=self.read(self.program,16777216)
            versions=set(re.findall(rb'Liner notes ([0-9]+(?:\.[0-9]+){1,3}(?:[-+][A-Za-z0-9.-]+)?)\r\n\x00',binary_data))
            if len(versions)==1:
                self.add('config','ok',location=self.program,detail='embedded version='+versions.pop().decode('ascii')+'; sha256='+hashlib.sha256(binary_data).hexdigest())
            else:
                self.add('config','limited','warning',self.program,detail='binary version marker unavailable/ambiguous; program was not executed')
        except (ValueError,OSError):
            self.add('config','limited','warning',self.program,detail='binary version unavailable; program was not executed')
        if self.service_program.get('core') and self.service_program['core']!=self.program:
            self.add('config','mismatch','warning',self.program,detail='systemd ExecStart differs; use --program for an intentional override')
        for name in ('openssl', 'sqlite3'):
            self.add('config', 'ok', detail=name + ' (Python runtime)')
        for tool, args in [('rpm', ['-q', '--qf', '%{VERSION}-%{RELEASE}\n', 'liner-notes']),
                           ('dpkg-query', ['-W', '-f=${Version}\n', 'liner-notes']),
                           ('pkg', ['query', '%v', 'liner-notes'])]:
            result = self.command(tool, args)
            if result and result[0] == 0 and re.fullmatch(r'[A-Za-z0-9_.+~:\-]+\s*', result[1]):
                self.add('config', 'ok', detail=tool + ': liner-notes ' + result[1].strip())
                break
        else:
            self.add('config', 'limited', 'warning', detail='package version unavailable; executable is not run')
        binary = self.path(self.program)
        result = self.command('readelf', ['-d', str(binary)]) if binary.is_file() else None
        if result and result[0] == 0:
            needed = re.findall(r'\(NEEDED\).*?\[([A-Za-z0-9_.+\-]+)\]', result[1])
            search=['/lib','/lib64','/usr/lib','/usr/lib64',self.prefix+'/lib']
            for value in re.findall(r'\((?:RPATH|RUNPATH)\).*?\[([^\]]+)\]',result[1]):
                search += [x for x in value.split(':') if x.startswith('/') and '$' not in x]
            cache=self.command('ldconfig',['-p'] if self.system=='Linux' else ['-r']) if self.system in {'Linux','FreeBSD'} else None
            for library in needed:
                found=any((Path(folder)/library).is_file() for folder in search)
                if not found and cache and cache[0]==0:
                    found=bool(re.search(r'(?<![\w.])'+re.escape(library)+r'(?![\w.])',cache[1]))
                self.add('config','ok' if found else 'library','ok' if found else 'warning',detail='ELF dependency: '+library)
            self.add('config', 'limited', location=self.program, detail='ELF dependencies inspected without ldd or executing the binary; custom loader paths may need review')
        pid = self.pid.get('core')
        if pid and self.system == 'Linux' and not self.staged:
            try:
                actual = Path('/proc') / str(pid) / 'exe'
                if actual.resolve() != binary.resolve():
                    self.add('config', 'mismatch', 'error', self.program)
                else:
                    self.add('config', 'ok', location=self.program, detail='running executable matches')
            except OSError:
                self.add('config', 'limited', 'warning', self.program)
        elif not pid:
            self.add('config', 'notrunning', 'warning')
        elif self.system=='FreeBSD':
            result=self.command('procstat',['-b',str(pid)])
            rows=result[1].splitlines()[1:] if result and result[0]==0 else []
            actual=rows[0].split()[-1] if rows and rows[0].split() else ''
            if not actual:
                self.add('config','notrunning','warning')
            elif actual!=self.program:
                self.add('config','mismatch','error',self.program)
            else:
                self.add('config','ok',location=self.program,detail='running executable matches')
        self.listener('config','core',self.core.get('port',2222))
        for field in ('limits', 'menu-dir', 'help-dir', 'admins', 'link-accounts'):
            value = self.core.get(field)
            if value:
                self.item('config', value, directory=field.endswith('-dir'))
        if self.core.get('limits'):
            self.check_limits(self.core['limits'])
        if self.core.get('admins'):
            try:
                self.admin_ids(self.core['admins'])
                self.add('config','limited','warning',self.core['admins'],detail='administrator ID syntax checked; member existence/status requires runtime verification')
            except (ValueError,UnicodeError,OSError):
                self.add('config','syntax','error',self.core['admins'])
        if 'web' in self.enabled:
            self.check_web()

    def check_limits(self,value):
        maxima=dict(members=999999,boards=1024,input_bytes=4096,body_bytes=262144,file_bytes=1073741824,batch_files=16,batch_bytes=17179869184,connections=128,transfers=24)
        try:
            seen=set()
            for line in self.read(value).decode('ascii').splitlines():
                if not line or line.startswith('#'):continue
                key,number=line.split('=',1)
                if key not in maxima or key in seen or not number.isdigit() or not 1<=int(number)<=maxima[key] or len(line)>=255:
                    raise ValueError('quota')
                seen.add(key)
            self.add('config','ok',location=value,detail='operational quota syntax and compiled maxima')
        except (ValueError,UnicodeError,OSError):
            self.add('config','syntax','error',value)

    def admin_ids(self,value):
        ids=set()
        for line in self.read(value).decode('ascii').splitlines():
            if not line or line.startswith('#'):continue
            if len(line)>=127 or not line.isdigit() or not 1<=int(line)<=999999 or int(line) in ids:
                raise ValueError('administrator IDs')
            ids.add(int(line))
        return ids

    def listener(self,group,feature,port):
        """Inspect kernel socket listings only. No connect(), DNS or health request."""
        pid=self.pid.get(feature)
        if not pid or self.staged:return
        try:
            if self.system=='Linux':
                descriptors=Path('/proc')/str(pid)/'fd'
                inodes=set()
                for fd in descriptors.iterdir():
                    try:target=os.readlink(fd)
                    except OSError:continue
                    match=re.fullmatch(r'socket:\[(\d+)\]',target)
                    if match:inodes.add(match[1])
                rows=self.read('/proc/'+str(pid)+'/net/tcp',4*1048576).decode('ascii').splitlines()[1:]
                found=any(len(fields:=line.split())>9 and fields[3]=='0A' and fields[9] in inodes and int(fields[1].split(':')[1],16)==port for line in rows)
            elif self.system=='FreeBSD':
                query=self.command('sockstat',['-4','-l','-P','tcp','-p',str(port)])
                if not query or query[0]!=0:raise OSError('kernel query unavailable')
                found=any(len(fields:=line.split())>2 and fields[2]==str(pid) for line in query[1].splitlines()[1:])
            else:
                self.add(group,'limited','warning',detail='listener observation unavailable on this OS')
                return
            self.add(group,'ok' if found else 'notrunning','ok' if found else 'warning',detail=feature+' listener port='+str(port)+' (kernel listing; no connection)')
        except (OSError,ValueError,UnicodeError):
            self.add(group,'limited','warning',detail='listener observation unavailable; no connection attempted')

    def check_web(self):
        file = self.args.web_config or self.config_dir + '/web/web.toml'
        try:
            cfg = tomllib.loads(self.read(file).decode('utf-8'))
            web, bbs = cfg['web'], cfg['bbs']
            origin = urlsplit(web['origin'].rstrip('/'))
            if origin.scheme not in {'http', 'https'} or not origin.netloc or origin.path or origin.query or origin.fragment or origin.username:
                raise ValueError('origin')
            if not re.fullmatch(r'/(?:[A-Za-z0-9_-]+/)*', web.get('base_path','/notes/')):
                raise ValueError('base')
            for port in (web.get('port',8088), bbs['port']):
                if type(port) is not int or not 1 <= port <= 65535:
                    raise ValueError('port')
            if not web.get('secure_cookies',True) and web.get('listen','127.0.0.1') not in {'127.0.0.1','::1'}:
                raise ValueError('cookie')
            ZoneInfo(web.get('timezone','Asia/Tokyo'))
            self.add('config', 'ok', location=file)
            self.listener('config','web',web.get('port',8088))
            if bbs.get('host') in {'127.0.0.1','localhost'} and bbs['port'] != self.core.get('port'):
                self.add('config', 'mismatch', 'error', file)
            for key in ('spool','messages','templates'):
                if key in web:
                    value = str(PurePosixPath(file).parent / web[key])
                    self.item('config', value, user='liner_notes_web', directory=True, writable=key=='spool', private=key=='spool',optional=key=='templates')
                    if key=='templates':
                        for name in ('index.html','components.html'):
                            template=str(PurePosixPath(value)/name)
                            if not self.path(template).exists():
                                template=self.prefix+'/share/liner-notes/web/webui/templates/'+name
                            if self.item('config',template,user='liner_notes_web'):
                                try:self.read(template,2097152).decode('utf-8')
                                except (UnicodeError,ValueError):self.add('config','syntax','error',template)
                    if key=='messages':
                        for name in ('ja.json','en.json'):
                            message=str(PurePosixPath(value)/name)
                            if self.item('config',message,user='liner_notes_web'):
                                content=json.loads(self.read(message,65536).decode('utf-8'))
                                if not isinstance(content,dict) or any(not isinstance(content.get(k,''),str) for k in ('name','tagline','banner','welcome','news','goodbye','system','birthday')):
                                    raise ValueError('messages')
                                greetings=content.get('greetings',{})
                                if not isinstance(greetings,dict) or not all(isinstance(v,str) for v in greetings.values()):
                                    raise ValueError('greetings')
            if importlib.util.find_spec('aiohttp') is None:
                self.add('config', 'library', 'error', detail='aiohttp')
        except (KeyError, TypeError, ValueError, UnicodeError):
            self.add('config', 'syntax', 'error', file)
        except OSError:
            self.add('config', 'unreadable', 'error', file)

    def snapshot_database(self, group, value, kind):
        """Never give SQLite the source path: locks/SHM/WAL exist only in temp copy."""
        path = self.path(value)
        sources = [path, Path(str(path)+'-wal'), Path(str(path)+'-journal')]
        before=None
        def metadata():
            result = {}
            for p in sources:
                try:
                    s = p.lstat()
                    if not stat.S_ISREG(s.st_mode) or s.st_nlink != 1:
                        raise ValueError('unsafe database')
                    result[p] = fingerprint(s)
                except FileNotFoundError:
                    result[p] = None
            return result
        def failed(code,status='error'):
            try:changed=before is not None and metadata()!=before
            except (OSError,ValueError):changed=before is not None
            self.add(group,'busy' if changed else code,'warning' if changed else status,value)
        try:
            deadline = time.monotonic() + self.args.database_timeout
            before = metadata()
            if before[path] is None:
                self.add(group, 'missing', 'error', value)
                return
            if before[sources[2]] and before[sources[2]][2]:
                self.add(group, 'busy', 'warning', value)
                return
            size = sum(v[2] for v in before.values() if v)
            if size > self.args.max_database_bytes:
                self.add(group, 'limit', 'warning', value)
                return
            with tempfile.TemporaryDirectory(prefix='liner-doctor-') as temporary:
                os.chmod(temporary, 0o700)
                if shutil.disk_usage(temporary).free < size * 2 + 16777216:
                    self.add(group, 'space', 'warning', value)
                    return
                copy = Path(temporary) / 'snapshot.sqlite'
                for src in sources[:2]:
                    if before[src] is None:
                        continue
                    dst = copy if src == path else Path(str(copy)+'-wal')
                    with open_regular(src,self.args.max_database_bytes,expected=before[src]) as inp, dst.open('xb') as out:
                        os.chmod(dst, 0o600)
                        remaining=before[src][2]
                        while remaining:
                            if time.monotonic()>deadline:
                                self.add(group,'limit','warning',value)
                                return
                            block=inp.read(min(remaining,1048576))
                            if not block:
                                self.add(group,'busy','warning',value)
                                return
                            out.write(block);remaining-=len(block)
                if metadata() != before:
                    self.add(group, 'busy', 'warning', value)
                    return
                with closing(sqlite3.connect(copy.as_uri()+'?mode=ro', uri=True, timeout=0)) as db:
                    db.execute('PRAGMA query_only=ON')
                    db.execute('PRAGMA trusted_schema=OFF')
                    db.set_progress_handler(lambda: int(time.monotonic() > deadline), 1000)
                    if db.execute('PRAGMA quick_check').fetchone() != ('ok',):
                        raise ValueError('integrity')
                    tables = {r[0] for r in db.execute("SELECT name FROM sqlite_master WHERE type='table'")}
                    if kind == 'core':
                        if not {'storage_meta','streams','blocks'} <= tables:
                            raise ValueError('schema')
                        versions = list(db.execute('SELECT version FROM storage_meta WHERE id=1'))
                        if len(versions)!=1 or versions[0][0] not in (1,2,3):
                            raise ValueError('version')
                    elif not {'events','cursors'} <= tables:
                        raise ValueError('link schema')
                    if db.execute('PRAGMA foreign_key_check').fetchone() is not None:
                        raise ValueError('foreign key')
                if metadata() != before:
                    self.add(group, 'busy', 'warning', value)
                else:
                    self.add(group, 'ok', location=value, detail='quick_check/schema/foreign keys; private snapshot including WAL; source never opened by SQLite')
        except InspectionChanged:
            self.add(group,'busy','warning',value)
        except (FileNotFoundError, PermissionError, OSError):
            failed('unreadable')
        except sqlite3.OperationalError as error:
            failed('limit' if str(error)=='interrupted' else 'database','warning' if str(error)=='interrupted' else 'error')
        except (sqlite3.DatabaseError, ValueError):
            failed('database')

    def check_database(self):
        value = self.core.get('database-structured') or self.core.get('database')
        if value:
            self.snapshot_database('database', value, 'core')
        else:
            # Journal parsing belongs to maintenance verify, which has its own backups.
            for field, default in [('board','notes.dat'),('members','members.dat')]:
                self.item('database', str(PurePosixPath(self.data_dir)/self.core.get(field,default)))
            self.add('database', 'limited', 'warning', detail='legacy journals: structural verification requires liner-notes-admin verify on a backup')

    def check_links(self):
        if 'links' not in self.enabled:
            self.add('links', 'disabled')
            return
        file = self.args.link_config or self.config_dir + '/link.toml'
        try:
            cfg = tomllib.loads(self.read(file).decode('utf-8'))
            node, bbs = cfg['node'], cfg['bbs']
            self.link_config = cfg
            boards = {}
            for entry in cfg['boards']:
                ident, local = entry['id'], entry['local']
                if not isinstance(ident,str) or not re.fullmatch('[0-9a-f]{64}',ident) or type(local) is not int or not 1<=local<=1024 or ident in boards or local in boards.values():
                    raise ValueError('board')
                boards[ident] = local
            if not boards or not cfg.get('peers'):
                raise ValueError('empty peers/boards')
            for port in (node.get('port',9044), bbs.get('port',23)):
                if type(port) is not int or not 1<=port<=65535:
                    raise ValueError('port')
            if not isinstance(bbs['login'],str) or not bbs['login'] or any(c.isspace() for c in bbs['login']):
                raise ValueError('login')
            for name, default, low, high in [('interval',5,1,3600),('scan_budget',128,1,8192),('pull_batches',16,1,1024),('max_events',1000000,1,2**63-1),('max_bytes',1073741824,1048576,2**63-1)]:
                value = node.get(name,default)
                if type(value) is not int or not low <= value <= high:
                    raise ValueError('limits')
            if type(node.get('rotate_tls',False)) is not bool:
                raise ValueError('rotation')
            self.add('links', 'ok', location=file, detail=f'{len(boards)} shared boards; no network probes')
            self.listener('links','links',node.get('port',9044))
            secret = bbs['password_file']
            self.item('links',secret,user='liner_notes_link',private=True)
            self.read(secret,8192,secret=True)
            self.check_link_certificates(cfg, boards)
            self.item('links',node['identity'],user='liner_notes_link',directory=True,private=True)
            self.item('links',str(PurePosixPath(node['database']).parent),user='liner_notes_link',directory=True,writable=True,private=True)
            account_file = self.core.get('link-accounts')
            if account_file:
                pairs = {}
                for line in self.read(account_file).decode().splitlines():
                    if not line.strip() or line.lstrip().startswith('#'):
                        continue
                    parts = line.split()
                    if len(parts)!=2 or not all(x.isdigit() for x in parts) or not 1<=int(parts[0])<=1024 or not 1<=int(parts[1])<=999999:
                        raise ValueError('allowlist')
                    if int(parts[0]) in pairs:
                        raise ValueError('duplicate allowlist')
                    pairs[int(parts[0])] = int(parts[1])
                if not set(boards.values())<=set(pairs):
                    self.add('links','mismatch','error',account_file)
                if self.core.get('admins'):
                    administrator_ids=self.admin_ids(self.core['admins'])
                    if administrator_ids.intersection(pairs.values()):
                        self.add('links','permission','error',account_file,detail='connector account is also configured as a global administrator')
                self.add('links','limited','warning',account_file, 'allowlist IDs checked; authentication/account suspension and board ACL require an authorised runtime check')
            else:
                self.add('links','missing','error',detail='link account allowlist')
            if bbs.get('host') in {'127.0.0.1','localhost'} and bbs.get('port',23)!=self.core.get('port'):
                self.add('links','mismatch','error',file)
            self.snapshot_database('links',node['database'],'link')
            if node.get('control_socket'):
                self.socket_item('links',node['control_socket'])
        except (KeyError, TypeError, ValueError, UnicodeError):
            self.add('links','syntax','error',file)
        except OSError:
            self.add('links','unreadable','error',file)

    def check_link_certificates(self, cfg, boards):
        try:
            from cryptography import x509
            from cryptography.exceptions import InvalidSignature
            from cryptography.hazmat.primitives import serialization
            from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey
        except ImportError:
            self.add('links','library','error',detail='cryptography (link feature)')
            return
        directory = cfg['node']['identity']
        def certificate(filename):
            cert = x509.load_pem_x509_certificate(self.read(filename,262144))
            public = cert.public_key()
            now = datetime.datetime.now(datetime.timezone.utc)
            if not isinstance(public,Ed25519PublicKey) or not cert.not_valid_before_utc <= now <= cert.not_valid_after_utc:
                raise ValueError('certificate')
            if cert.subject==cert.issuer:
                public.verify(cert.signature,cert.tbs_certificate_bytes)
            return public.public_bytes(serialization.Encoding.Raw,serialization.PublicFormat.Raw)
        try:
            key_file = str(PurePosixPath(directory)/'identity.key')
            self.item('links',key_file,user='liner_notes_link',private=True)
            key = serialization.load_pem_private_key(self.read(key_file,262144,secret=True),None)
            public = certificate(str(PurePosixPath(directory)/'identity.crt'))
            if not isinstance(key,Ed25519PrivateKey) or key.public_key().public_bytes(serialization.Encoding.Raw,serialization.PublicFormat.Raw)!=public:
                raise ValueError('key mismatch')
            own = hashlib.sha256(public).hexdigest()
            for section in ('origins','peers'):
                seen = {own}
                for entry in cfg.get(section,[]):
                    remote = hashlib.sha256(certificate(entry['certificate'])).hexdigest()
                    allowed = entry['boards']
                    if remote in seen or not allowed or len(allowed)!=len(set(allowed)) or not set(allowed)<=set(boards):
                        raise ValueError('assignment')
                    seen.add(remote)
                    if section=='peers' and (not isinstance(entry['host'],str) or not entry['host'] or type(entry.get('port',9044)) is not int or not 1<=entry.get('port',9044)<=65535):
                        raise ValueError('endpoint')
            self.add('links','ok',location=directory,detail='Ed25519 identity/certificates/trust assignments; no secrets emitted')
        except (ValueError,TypeError,KeyError,OSError,InvalidSignature):
            self.add('links','certificate','error',directory)

    def socket_item(self, group, value):
        try:
            path = self.path(value)
            if not PurePosixPath(value).is_absolute() or not stat.S_ISSOCK(path.lstat().st_mode):
                self.add(group,'unsafe','error',value)
            else:
                self.add(group,'ok',location=value,detail='socket path only; not connected')
        except OSError:
            self.add(group,'notrunning','warning',value)

    def check_doors(self):
        file = self.core.get('doors')
        if not file:
            required='doors' in self.enabled
            self.add('doors','missing' if required else 'disabled','error' if required else 'ok',detail='doors configuration' if required else '')
            return
        try:
            lines = self.read(file).decode('utf-8').splitlines()
            ids = set()
            for line in lines:
                if not line or line.startswith('#'):
                    continue
                parts = line.split('|')
                if len(parts) not in (6,7) or len(line.encode())>=4095 or len(ids)>=32:
                    raise ValueError('door syntax')
                ident,level,seconds,exe,directory,title = parts[:6]
                if not all(x.isdigit() for x in (ident,level,seconds)) or not 1<=int(ident)<=86400 or not 0<=int(level)<=5 or not 1<=int(seconds)<=86400 or ident in ids:
                    raise ValueError('door numbers')
                ids.add(ident)
                if not PurePosixPath(exe).is_absolute() or not PurePosixPath(directory).is_absolute() or len(exe.encode())>1023 or len(directory.encode())>1023 or not title or len(title.encode())>160 or any(ord(c)<32 or ord(c)==127 for c in title):
                    raise ValueError('door fields')
                drop, slots = 'none', 0
                if len(parts)==7:
                    match = re.fullmatch(r'(none|dorinfo1):([1-9][0-9]*)',parts[6])
                    if not match or not 1<=int(match[2])<=32:
                        raise ValueError('slots')
                    drop,slots = match[1],int(match[2])
                executable_ok=self.item('doors',exe,executable=True)
                self.item('doors',directory,directory=True,writable=True)
                if self.user=='root':
                    self.add('doors','permission','error',file,detail='runtime refuses Door execution as root')
                try:
                    if not executable_ok:continue
                    with open_regular(self.path(exe)) as stream:
                        first = stream.readline(4096)
                    if first.startswith(b'#!'):
                        interpreter = first[2:].decode().strip().split()[0]
                        self.item('doors',interpreter,executable=True)
                    if PurePosixPath(exe).name=='door_clans.sh':
                        for name in ('clans','clans.cfg'):
                            self.item('doors',str(PurePosixPath(directory)/name),executable=name=='clans')
                        if drop!='dorinfo1' or not slots:
                            self.add('doors','syntax','error',file,detail='Clans wrapper requires dorinfo1 and a slot')
                    self.add('doors','limited','warning',location=exe,detail=f'drop={drop}; slots={slots}; external game/runtime dependencies require operator review; not executed')
                except (UnicodeError,ValueError):
                    self.add('doors','limited','warning',exe)
            self.add('doors','ok',location=file,detail=f'{len(ids)} games; drop files and slot locks were not created')
        except (ValueError,UnicodeError):
            self.add('doors','syntax','error',file)
        except OSError:
            self.add('doors','unreadable','error',file)

    def check_transfers(self):
        helper = self.core.get('zmodem-helper')
        if not helper:
            required='transfers' in self.enabled
            self.add('transfers','missing' if required else 'disabled','error' if required else 'ok',detail='transfer helper configuration' if required else '')
            return
        self.item('transfers',helper,executable=True)
        vault = self.core.get('vault-dir')
        if vault:
            self.item('transfers',vault,directory=True,writable=True,private=True)
        spool = self.env.get('LINER_NOTES_SPOOL_ROOT',self.data_dir+'/spool')
        self.item('transfers',spool,directory=True,writable=True,private=True)
        lrz = self.env.get('LINER_NOTES_LRZ',self.prefix+'/bin/lrz')
        lsz = self.env.get('LINER_NOTES_LSZ',self.prefix+'/bin/lsz')
        for value in (lrz,lsz):
            self.item('transfers',value,executable=True)
        receipt = self.env.get('LINER_NOTES_RESUME_RECEIPT',self.data_dir+'/lrz-resume.json')
        self.item('transfers',receipt,private=True,optional=True)
        try:
            value = json.loads(self.read(receipt,8192,secret=True))
            binary = self.read(lrz,16777216)
            if value.get('patch')!='crc32-lp64-v1' or value.get('sha256')!=hashlib.sha256(binary).hexdigest():
                raise ValueError('receipt mismatch')
            self.add('transfers','ok',location=receipt,detail='receiver hash matches real-resume receipt; no transfer executed')
        except (OSError,ValueError,UnicodeError):
            self.add('transfers','resume','warning',receipt)
        for tool,args in [('rpm',['-q','--qf','%{VERSION}\n','lrzsz']),('dpkg-query',['-W','-f=${Version}\n','lrzsz']),('pkg',['query','%v','lrzsz'])]:
            result=self.command(tool,args)
            if result and result[0]==0 and re.fullmatch(r'[A-Za-z0-9_.+~:\-]+\s*',result[1]):
                self.add('transfers','ok',detail='lrzsz package '+result[1].strip())
                break
        else:
            self.add('transfers','limited','warning',detail='lrzsz package version unavailable; helper binaries not executed')
        try:
            free=shutil.disk_usage(self.path(spool)).free
            self.add('transfers','space' if free<1073741824 else 'ok','warning' if free<1073741824 else 'ok',spool, f'free_bytes={free}')
        except OSError:
            self.add('transfers','unreadable','warning',spool)

    def check_permissions(self):
        self.item('permissions',self.data_dir,directory=True,writable=True,private=True)
        self.item('permissions',self.program,executable=True)
        database=self.core.get('database-structured') or self.core.get('database')
        if database:
            self.item('permissions',database,writable=True,private=True)
        for field in ('user-log','chat-log'):
            value=self.core.get(field)
            if value and value!='-':
                self.item('permissions',str(PurePosixPath(value).parent),directory=True,writable=True)
        self.add('permissions','limited',detail='mode/owner/group and ancestor traversal only; ACL/systemd sandbox must also permit access')

    def check_selinux(self):
        if self.system!='Linux':
            self.add('selinux','disabled',detail='SELinux is Linux-specific')
            return
        result=self.command('getenforce',[])
        if not result:
            self.add('selinux','disabled' if not self.args.require_selinux else 'selinux','ok' if not self.args.require_selinux else 'error')
            return
        mode=result[1].strip()
        if mode!='Enforcing':
            self.add('selinux','selinux','error' if self.args.require_selinux else 'warning',detail='mode='+mode if mode in {'Disabled','Permissive','Enforcing'} else 'mode unavailable')
            return
        self.add('selinux','ok',detail='Enforcing')
        modules=self.command('semodule',['-l'])
        policy_required=self.args.require_selinux
        if not modules or modules[0]!=0:
            self.add('selinux','unreadable','warning',detail='policy inventory (administrator query may be required)')
        elif not any(line.split() and line.split()[0]=='liner_notes' for line in modules[1].splitlines()):
            self.add('selinux','selinux','error' if self.args.require_selinux else 'warning',detail='liner_notes policy module missing')
        else:
            policy_required=True
            self.add('selinux','ok',detail='liner_notes policy module')
        for feature,domain in [('core','liner_notes_t'),('web','liner_notes_web_t'),('links','liner_notes_link_t')]:
            pid=self.pid.get(feature)
            if not pid:
                if feature=='core' or feature in self.enabled:
                    self.add('selinux','notrunning','warning',detail=feature)
                continue
            try:
                context=self.read('/proc/'+str(pid)+'/attr/current',4096).decode().strip('\x00\n')
                actual=context.split(':')[2]
                self.add('selinux','ok' if actual==domain else 'unconfined','ok' if actual==domain else 'error' if policy_required else 'warning',detail=feature+': '+actual)
            except (OSError,ValueError,UnicodeError,IndexError):
                self.add('selinux','unreadable','warning',detail=feature)
        labels=[(self.program,'liner_notes_exec_t'),(self.data_dir,'liner_notes_data_t')]
        if 'transfers' in self.enabled:
            labels.append((self.env.get('LINER_NOTES_SPOOL_ROOT',self.data_dir+'/spool'),'liner_notes_spool_t'))
            if self.core.get('zmodem-helper'):
                labels.append((self.core['zmodem-helper'],'liner_notes_transfer_exec_t'))
            receipt=self.env.get('LINER_NOTES_RESUME_RECEIPT',self.data_dir+'/lrz-resume.json')
            if self.path(receipt).exists():labels.append((receipt,'liner_notes_transfer_receipt_t'))
        if 'web' in self.enabled:
            labels += [(self.prefix+'/sbin/liner-notes-web','liner_notes_web_exec_t'),('/var/lib/liner-notes-web','liner_notes_web_data_t')]
        if 'links' in self.enabled:
            labels += [(self.prefix+'/bin/liner-notes-link','liner_notes_link_exec_t')]
            cfg=getattr(self,'link_config',{})
            if cfg:
                labels += [(cfg['node']['identity'],'liner_notes_link_secret_t'),(cfg['bbs']['password_file'],'liner_notes_link_secret_t'),(cfg['node']['database'],'liner_notes_link_data_t')]
        for path,expected in labels:
            current=self.command('ls',['-Zd','--',path])
            mapping=self.command('matchpathcon',['-n',path])
            if current and current[0]==0:
                tokens=current[1].split()
                context=next((x for x in tokens if re.fullmatch(r'[^: ]+:[^: ]+:[^: ]+:.*',x)), '')
                actual=context.split(':')[2] if context else ''
                self.add('selinux','ok' if actual==expected else 'selinux','ok' if actual==expected else 'error' if policy_required else 'warning',path, 'type='+actual)
                if mapping and mapping[0]==0 and mapping[1].strip()!=context:
                    self.add('selinux','selinux','warning',path,detail='label differs from configured mapping; local override is not changed')
            else:
                self.add('selinux','unreadable','warning',path)

    def run(self):
        self.load()
        for check in CHECKS:
            if check in self.selected:
                try:
                    getattr(self,'check_'+check)()
                except (OSError,ValueError,TypeError,KeyError):
                    self.add(check,'limited','warning',detail='inspection could not be completed; no configuration or source data was changed')
        statuses={r['status'] for r in self.results}
        code=2 if 'error' in statuses else 1 if 'warning' in statuses else 0
        return dict(schema=1,read_only=True,os=self.system,exit_code=code,results=self.results)


def parser():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--check-all',action='store_true')
    for name in CHECKS:
        p.add_argument('--check-'+name,action='store_true')
    p.add_argument('--json',action='store_true')
    p.add_argument('--lang',choices=('ja','en'),default='ja')
    p.add_argument('--root',default='/',help='Inspect a staged filesystem; disables host process/package queries')
    p.add_argument('--os',choices=('Linux','FreeBSD','SunOS'),help='Layout of the inspected filesystem')
    for name in ('prefix','config-dir','config','program','data-dir','database','service-user','link-config','web-config','doors-config'):
        p.add_argument('--'+name)
    p.add_argument('--enable',choices=('web','links','doors','transfers'),action='append',help='Diagnose an enabled optional service without starting it')
    p.add_argument('--no-rc-conf',action='store_true',help='Do not read FreeBSD rc.conf overrides')
    p.add_argument('--require-selinux',action='store_true')
    p.add_argument('--max-database-bytes',type=int,default=1073741824)
    p.add_argument('--database-timeout',type=int,default=30)
    return p


def main():
    p=parser();a=p.parse_args()
    if not a.check_all and not any(getattr(a,'check_'+x) for x in CHECKS):
        p.error('select --check-all or at least one --check-*')
    if a.max_database_bytes<1 or not 1<=a.database_timeout<=3600:
        p.error('invalid diagnostic resource limits')
    report=Doctor(a).run()
    if a.json:
        print(json.dumps(report,ensure_ascii=False,indent=2))
    else:
        for r in report['results']:
            print(f"[{r['status'].upper()}] {r['check']}: {r['message']} {r['location']} {r['detail']}")
            if r['status']!='ok':
                print('  '+r['remedy'])
    return report['exit_code']


if __name__=='__main__':
    raise SystemExit(main())
