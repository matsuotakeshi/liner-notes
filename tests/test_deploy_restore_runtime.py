#!/usr/bin/env python3
"""Hosted disposable runner only: restore and boot the actual BBS as nobody."""
import hashlib
import os
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from deploy_host import trusted
from deploy_restore import verify_application
from test_board import Server, post
from test_runtime import Client
from test_members import auth, PASSWORD
from test_mail import send

assert os.environ.get('GITHUB_ACTIONS') == 'true' and os.geteuid() == 0
binary = Path('/usr/local/libexec/liner-notes-deploy-ci/runtime')
trusted(binary)
with tempfile.TemporaryDirectory(prefix='liner-cd-restore-ci-', dir='/var/tmp') as temporary:
    base = Path(temporary)
    data = base / 'original'
    data.mkdir()
    (data / 'catalog').write_text('1|0|all|member|Restore test\n')
    options = ['--database-structured', str(data / 'liner.sqlite')]
    server = Server(str(binary), data / 'notes', members=data / 'members', catalog=data / 'catalog', options=options)
    clients = [Client(server.port) for _ in range(3)]
    try:
        for client, name in zip(clients, ('alice', 'bob', 'carol')):
            auth(client, name, True)
        post(clients[0], 'restore article', 'restore article body')
        send(clients[0], 'carol', 'restore mail', 'restore mail body')
    finally:
        for client in clients:
            client.close()
        server.close()
    before = {str(p.relative_to(data)): hashlib.sha256(p.read_bytes()).hexdigest() for p in data.rglob('*') if p.is_file()}
    restored = base / 'restored'
    destination = restored / str(data).lstrip('/')
    destination.parent.mkdir(parents=True)
    shutil.copytree(data, destination)
    def login(name):
        return [{'expect': 'RTBBS-PROBE '}, {'send': 'LOGIN ' + name + '\n', 'expect': 'PASSWORD'},
                {'send': PASSWORD + '\n', 'expect': 'LOGGED IN ' + name}]
    profile = {'run_user': 'nobody', 'executable': str(binary), 'data_root': str(data),
               'structured': True, 'database': str(data / 'liner.sqlite'), 'board': str(data / 'notes'),
               'members': str(data / 'members'), 'catalog': str(data / 'catalog'),
               'probes': {
                   'login': {'exchanges': login('alice')},
                   'article': {'exchanges': login('bob') + [{'send': 'READ 1\n', 'expect': 'restore article body'}]},
                   'mail': {'exchanges': login('carol') + [{'send': 'MAIL READ 1\n', 'expect': 'restore mail body'}]}}}
    verify_application(restored, profile, trusted)
    after = {str(p.relative_to(data)): hashlib.sha256(p.read_bytes()).hexdigest() for p in data.rglob('*') if p.is_file()}
    assert before == after, 'Live source changed during restored application verification'
print('PASS actual runtime restored copy: unprivileged login/article/mail; original hashes unchanged')
