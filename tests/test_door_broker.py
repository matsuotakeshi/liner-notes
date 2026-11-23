"""Broker authority, one-use/revocation, UID roles and bounded protocol tests."""
import asyncio,json,sys,unittest
from unittest.mock import patch,Mock
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from door_broker_core import Broker,Denied
from door_broker import Server
from door_auth_client import DoorAuthorization,AuthorizationError
import door_auth_client

class Core(unittest.TestCase):
    def setUp(self):
        self.now=100;self.b=Broker('bbs-a',{40:[1],41:[2]},clock=lambda:self.now,ttl=10,session_ttl=60,capacity=4)
    def opened(self,member=1,session='s1',generation=1,door=1):
        s=self.b.open(member,door,session,generation);return s,self.b.issue(s)
    def test_two_members_and_door_scope(self):
        s,a=self.opened();t,b=self.opened(2,'s2')
        one=self.b.consume(40,1,'bbs-a',a);two=self.b.consume(40,1,'bbs-a',b)
        self.assertEqual((one['claims']['member'],two['claims']['member']),(1,2))
        self.assertNotEqual(one['lease'],two['lease'])
        with self.assertRaises(Denied):self.b.consume(40,1,'bbs-a',a)
        with self.assertRaises(Denied):self.b.check(41,2,'bbs-a',one['lease'])
    def test_forgery_and_wrong_bbs_do_not_consume(self):
        s,token=self.opened()
        for uid,door,bbs,ticket in [(40,1,'foreign',token),(41,2,'bbs-a',token),(99,1,'bbs-a',token),(40,1,'bbs-a','0'*64),(40,1,'bbs-a','invalid')]:
            with self.assertRaises(Denied):self.b.consume(uid,door,bbs,ticket)
        self.assertEqual(self.b.consume(40,1,'bbs-a',token)['claims']['member'],1)
    def test_expiry_and_disconnect(self):
        s,token=self.opened();self.now+=11
        with self.assertRaises(Denied):self.b.consume(40,1,'bbs-a',token)
        token=self.b.issue(s);lease=self.b.consume(40,1,'bbs-a',token)['lease'];self.b.close(s)
        with self.assertRaises(Denied):self.b.check(40,1,'bbs-a',lease)
    def test_generation_old_close_does_not_revoke_new(self):
        old,token=self.opened();lease=self.b.consume(40,1,'bbs-a',token)['lease']
        new,token=self.opened(generation=2)
        with self.assertRaises(Denied):self.b.check(40,1,'bbs-a',lease)
        self.b.close(old)
        self.assertEqual(self.b.consume(40,1,'bbs-a',token)['claims']['generation'],2)
        with self.assertRaises(Denied):self.opened(generation=1)
    def test_issue_replaces_ticket_and_broker_restart_revokes(self):
        s,old=self.opened();new=self.b.issue(s)
        with self.assertRaises(Denied):self.b.consume(40,1,'bbs-a',old)
        lease=self.b.consume(40,1,'bbs-a',new)['lease']
        replacement=Broker('bbs-a',{40:[1]})
        with self.assertRaises(Denied):replacement.check(40,1,'bbs-a',lease)
    def test_session_timeout_and_capacity(self):
        s,token=self.opened();lease=self.b.consume(40,1,'bbs-a',token)['lease'];self.now+=61
        with self.assertRaises(Denied):self.b.check(40,1,'bbs-a',lease)
        for i in range(4):self.opened(session='n'+str(i))
        with self.assertRaises(Denied):self.opened(session='extra')
    def test_generation_zero_is_authenticated_and_not_reusable(self):
        s,token=self.opened(generation=0)
        result=self.b.consume(40,1,'bbs-a',token)
        self.assertEqual(result['claims']['generation'],0)
        self.b.close(s)
        with self.assertRaises(Denied):self.opened(generation=0)
        for value in (-1,True,2**32,'0'):
            with self.assertRaises(Denied):self.opened(session='other',generation=value)
        fresh,token=self.opened(generation=1)
        self.assertEqual(self.b.consume(40,1,'bbs-a',token)['claims']['generation'],1)
    def test_invalid_identity_types(self):
        for value in (True,0,-1,1000000,'1'):
            with self.assertRaises(Denied):self.opened(member=value)
        with self.assertRaises(Denied):self.opened(session='bad\nname')
        with self.assertRaises(Denied):self.opened(door=3)

class Load(unittest.TestCase):
    def test_128_live_members_and_repeated_generations_stay_bounded(self):
        now=[0]
        b=Broker('load',{40:[1]},clock=lambda:now[0],capacity=128,session_ttl=60)
        previous=[]
        for generation in range(8):
            live=[]
            for i in range(128):
                session=b.open(i+1,1,'s'+str(i),generation)
                ticket=b.issue(session)
                result=b.consume(40,1,'load',ticket)
                live.append((session,result['lease']))
                with self.assertRaises(Denied):b.consume(40,1,'load',ticket)
            self.assertEqual((len(b.sessions),len(b.leases),len(b.generations)),(128,128,128))
            self.assertFalse(b.tickets)
            with self.assertRaises(Denied):b.open(999,1,'overflow',0)
            for old,lease in previous:
                b.close(old)
                with self.assertRaises(Denied):b.check(40,1,'load',lease)
            for i,(_,lease) in enumerate(live):
                self.assertEqual(b.check(40,1,'load',lease)['member'],i+1)
            previous=live
        now[0]=61;b.sweep()
        self.assertFalse(b.sessions or b.leases or b.tickets or b.generations)
        b.open(1,1,'fresh-after-expiry',0)

class ScopedLaunches(unittest.TestCase):
    def setUp(self):
        self.b=Broker('bbs',{40:[1]},clock=lambda:100,capacity=4)

    def launch(self,n,member=1,scope='incarnation.slot1'):
        return self.b.open(member,1,'random-launch-'+str(n),0,scope=scope,sequence=n)

    def test_fresh_ids_do_not_exhaust_history_and_other_member_can_launch(self):
        for n in range(1,10001):
            session=self.launch(n)
            ticket=self.b.issue(session)
            lease=self.b.consume(40,1,'bbs',ticket)['lease']
            self.b.close(session)
        self.assertEqual((len(self.b.sessions),len(self.b.generations)),(0,1))
        with self.assertRaises(Denied):self.b.check(40,1,'bbs',lease)
        second=self.launch(10001,member=2)
        result=self.b.consume(40,1,'bbs',self.b.issue(second))
        self.assertEqual((result['claims']['member'],result['claims']['session'],result['claims']['generation']),
                         (2,'random-launch-10001',0))

    def test_old_sequence_close_and_token_cannot_affect_new_launch(self):
        old=self.launch(1);ticket=self.b.issue(old)
        lease=self.b.consume(40,1,'bbs',ticket)['lease'];self.b.close(old)
        new=self.launch(2);new_ticket=self.b.issue(new)
        for n in (1,2):
            with self.assertRaises(Denied):self.launch(n)
        self.b.close(old)
        self.b.consume(40,1,'bbs',new_ticket)
        with self.assertRaises(Denied):self.b.consume(40,1,'bbs',ticket)
        with self.assertRaises(Denied):self.b.check(40,1,'bbs',lease)

    def test_busy_slot_cannot_be_reassigned_and_failed_open_does_not_advance(self):
        old=self.launch(1)
        with self.assertRaises(Denied):self.launch(2,member=2)
        self.assertTrue(old.active)
        self.b.close(old);self.launch(2,member=2)

    def test_invalid_scope_sequence_and_scope_collision(self):
        for extra in (dict(scope='s'),dict(sequence=1),dict(scope='bad space',sequence=1),
                      dict(scope='s',sequence=True),dict(scope='s',sequence=0),
                      dict(scope='s',sequence=2**63)):
            with self.assertRaises(Denied):self.b.open(1,1,'session',0,**extra)
        first=self.launch(1)
        with self.assertRaises(Denied):self.b.open(2,1,first.session,1,scope='other',sequence=1)
        self.assertTrue(first.active)

    def test_scope_and_legacy_namespaces_and_total_capacity_remain_bounded(self):
        self.b.open(1,1,'incarnation.slot1',0)
        self.launch(1)
        self.launch(2,scope='slot2');self.launch(3,scope='slot3')
        with self.assertRaises(Denied):self.launch(4,scope='overflow')
        self.assertEqual(len(self.b.generations),4)


class Consumer(unittest.TestCase):
    def test_wrong_socket_peer_receives_no_credential(self):
        connection=Mock();connection.__enter__=Mock(return_value=connection);connection.__exit__=Mock(return_value=False)
        with patch.object(door_auth_client,'socket',Mock(socket=Mock(return_value=connection))),patch.object(door_auth_client,'peer_uid',return_value=41):
            with self.assertRaisesRegex(AuthorizationError,'identity mismatch'):
                door_auth_client.request('/fixture','consume',bbs='bbs',door=1,token='private-ticket',broker_uid=30)
        connection.sendall.assert_not_called()
        connection.recv.assert_not_called()
        connection.__exit__.assert_called_once()

    def test_broker_outage_never_uses_cached_authority(self):
        claims=dict(bbs='bbs',member=1,door=1,session='s',generation=0,ticket_id='id')
        with patch('door_auth_client.request',return_value=dict(lease='private-lease',claims=claims)):
            lease=DoorAuthorization('/fixture',bbs='bbs',door=1,ticket='private-ticket',broker_uid=30)
        with patch('door_auth_client.request',side_effect=AuthorizationError('authorization unavailable')) as rpc:
            for _ in range(3):
                with self.assertRaises(AuthorizationError):lease.authorize(1)
            self.assertEqual(rpc.call_count,3)
            self.assertTrue(all(call.args[1]=='check' for call in rpc.call_args_list))
        # A recovered endpoint cannot substitute any component of the binding.
        for field,value in [('bbs','other'),('member',2),('door',2),('session','new'),('generation',1),('ticket_id','new')]:
            with self.subTest(field=field),patch('door_auth_client.request',return_value=dict(claims,**{field:value})):
                with self.assertRaisesRegex(AuthorizationError,'binding mismatch'):lease.authorize()

    def test_rpc_deadline_covers_trickled_response(self):
        connection=Mock();connection.__enter__=Mock(return_value=connection);connection.__exit__=Mock(return_value=False)
        connection.recv.return_value=b' '
        with patch.object(door_auth_client,'socket',Mock(socket=Mock(return_value=connection))),patch.object(door_auth_client,'peer_uid',return_value=30),patch.object(door_auth_client.time,'monotonic',side_effect=[0,0,.5,1,2,3]):
            with self.assertRaises(AuthorizationError):
                door_auth_client.request('/fixture','check',bbs='bbs',door=1,token='secret',broker_uid=30)
        self.assertEqual(connection.recv.call_count,2)
        self.assertEqual([c.args[0] for c in connection.settimeout.call_args_list],[3,2.5,2,1])
        connection.__exit__.assert_called_once()

    def test_rpc_rejects_nonobject_response(self):
        connection=Mock();connection.__enter__=Mock(return_value=connection);connection.__exit__=Mock(return_value=False)
        connection.recv.side_effect=[b'[',b']',b'\n']
        with patch.object(door_auth_client,'socket',Mock(socket=Mock(return_value=connection))),patch.object(door_auth_client,'peer_uid',return_value=30):
            with self.assertRaises(AuthorizationError):
                door_auth_client.request('/fixture','check',bbs='bbs',door=1,token='secret',broker_uid=30)

    def test_mutations_recheck_identity_and_liveness(self):
        broker=Broker('bbs',{40:[1]});session=broker.open(1,1,'s',0);ticket=broker.issue(session)
        def rpc(path,operation,*,bbs,door,token,broker_uid):
            self.assertEqual(broker_uid,30)
            try:return (broker.consume if operation=='consume' else broker.check)(40,door,bbs,token)
            except Denied:raise AuthorizationError('denied') from None
        with patch('door_auth_client.request',side_effect=rpc):
            lease=DoorAuthorization('/fixture',bbs='bbs',door=1,ticket=ticket,broker_uid=30)
            self.assertEqual(lease.authorize(1)['member'],1)
            for target in (2,True,'1'):
                with self.assertRaises(AuthorizationError):lease.authorize(target)
            broker.close(session)
            with self.assertRaises(AuthorizationError):lease.authorize(1)

class Writer:
    def __init__(self,uid):self.uid=uid;self.data=bytearray();self.closed=False;self.transport=Mock()
    def get_extra_info(self,name):return self.uid
    def write(self,data):self.data.extend(data)
    async def drain(self):pass
    def close(self):self.closed=True
    async def wait_closed(self):pass

class Protocol(unittest.IsolatedAsyncioTestCase):
    async def live_issuer(self,name):
        blocked=asyncio.Event()
        class Reader(asyncio.StreamReader):
            calls=0
            async def readline(self):
                self.calls+=1
                if self.calls==2:blocked.set()
                return await super().readline()
        reader=Reader(limit=4096);writer=Writer(0)
        reader.feed_data(json.dumps(dict(op='open',member=1,door=1,session=name,generation=0)).encode()+b'\n')
        task=asyncio.create_task(self.server.handle(reader,writer))
        try:await asyncio.wait_for(blocked.wait(),5)
        except BaseException:
            reader.feed_eof();await asyncio.wait_for(task,5);raise
        return reader,writer,task
    async def request(self,uid,request):
        reader=asyncio.StreamReader(limit=4096);writer=Writer(uid)
        reader.feed_data(json.dumps(request).encode()+b'\n');reader.feed_eof()
        await self.server.handle(reader,writer);return bytes(writer.data)
    async def asyncSetUp(self):
        self.b=Broker('bbs',{40:[1]});self.server=Server(self.b,peer=lambda uid:uid)
    async def test_door_and_bbs_cannot_issue(self):
        request=dict(op='open',member=2,door=1,session='a',generation=1)
        for uid in (20,30,40):
            result=await self.request(uid,request);self.assertIn(b'"ok":false',result);self.assertNotIn(b'ticket',result)
        self.assertFalse(self.b.sessions)
    async def test_scoped_protocol_reuses_bounded_history_and_rejects_nonissuer(self):
        self.b.capacity=1
        for sequence in range(1,21):
            request=dict(op='open_scoped',member=sequence%2+1,door=1,
                         session='fresh'+str(sequence),generation=0,scope='boot.slot',sequence=sequence)
            self.assertIn(b'"ok":false',await self.request(40,request))
            result=json.loads(await self.request(0,request))
            self.assertTrue(result['ok'])
            self.assertIn(b'"ok":false',await self.request(0,request))
        self.assertEqual(len(self.b.generations),1)
        self.assertFalse(self.b.sessions or self.b.tickets or self.b.leases)
        missing=dict(request);del missing['sequence']
        self.assertIn(b'"ok":false',await self.request(0,missing))

    async def test_live_issuer_owns_lease(self):
        reader,writer,task=await self.live_issuer('s')
        token=json.loads(writer.data)['result']['ticket']
        result=json.loads(await self.request(40,dict(op='consume',door=1,bbs='bbs',token=token)))['result']
        self.assertEqual(result['claims']['member'],1)
        reader.feed_eof();await task
        check=await self.request(40,dict(op='check',door=1,bbs='bbs',token=result['lease']))
        self.assertIn(b'"ok":false',check)
    async def test_concurrent_consumers_have_one_winner(self):
        reader,writer,task=await self.live_issuer('race')
        try:
            ticket=json.loads(writer.data)['result']['ticket']
            request=dict(op='consume',door=1,bbs='bbs',token=ticket)
            results=[json.loads(raw) for raw in await asyncio.gather(*(self.request(40,request) for _ in range(16)))]
            winners=[r for r in results if r['ok']]
            self.assertEqual(len(winners),1)
            self.assertEqual(len(self.b.leases),1)
            self.assertFalse(self.b.tickets)
            lease=winners[0]['result']['lease']
            self.assertEqual(self.b.check(40,1,'bbs',lease)['session'],'race')
        finally:
            reader.feed_eof();await asyncio.wait_for(task,5)
        self.assertFalse(self.b.leases or self.server.clients)
    async def test_128_parallel_issuers_revoke_without_disturbing_survivors(self):
        live=await asyncio.gather(*(self.live_issuer('parallel'+str(i)) for i in range(128)))
        try:
            leases=[self.b.consume(40,1,'bbs',json.loads(writer.data)['result']['ticket'])['lease']
                    for reader,writer,task in live]
            self.assertEqual(len(self.server.clients),128)
            for reader,writer,task in live[:64]:reader.feed_eof()
            await asyncio.wait_for(asyncio.gather(*(task for _,_,task in live[:64])),5)
            for i,lease in enumerate(leases):
                if i<64:
                    with self.assertRaises(Denied):self.b.check(40,1,'bbs',lease)
                else:self.assertEqual(self.b.check(40,1,'bbs',lease)['session'],'parallel'+str(i))
        finally:
            for reader,writer,task in live:reader.feed_eof()
            await asyncio.wait_for(asyncio.gather(*(task for _,_,task in live)),5)
        self.assertFalse(self.server.clients or self.b.sessions or self.b.tickets or self.b.leases)
    async def test_malformed_request_has_no_secret_echo(self):
        result=await self.request(0,{'op':'wrong','secret':'do-not-echo'})
        self.assertIn(b'"ok":false',result);self.assertNotIn(b'do-not-echo',result)
    async def test_oversize_truncated_and_nonobject_requests_release_client(self):
        payloads=[b'x'*4097+b'\n',b'{"secret":"do-not-echo"}',b'[]\n',b'null\n',b'\xff\n',
                  b'['*1500+b'0'+b']'*1500+b'\n']
        for payload in payloads:
            with self.subTest(kind=payload[:1],length=len(payload)):
                reader=asyncio.StreamReader(limit=4096);writer=Writer(0)
                reader.feed_data(payload);reader.feed_eof()
                await self.server.handle(reader,writer)
                self.assertEqual(writer.data,b'{"ok":false,"error":"authorization denied"}\n')
                self.assertTrue(writer.closed)
                self.assertFalse(self.server.clients);self.assertFalse(self.b.sessions)
                self.assertFalse(self.b.tickets);self.assertFalse(self.b.leases)
    async def test_connection_capacity_recovers_after_issuer_disconnect(self):
        self.b.capacity=1
        reader,writer,task=await self.live_issuer('capacity')
        try:
            self.assertTrue(self.b.sessions)
            ticket=json.loads(writer.data)['result']['ticket']
            valid=json.loads(await self.request(40,dict(op='consume',door=1,bbs='bbs',token=ticket)))
            self.assertTrue(valid['ok'], 'Full issuer pool must still allow consuming its tickets')
            checked=json.loads(await self.request(40,dict(op='check',door=1,bbs='bbs',token=valid['result']['lease'])))
            self.assertTrue(checked['ok'], 'Full issuer pool must still allow live lease checks')
            denied=await self.request(40,dict(op='consume',door=1,bbs='bbs',token='0'*64))
            self.assertIn(b'"ok":false',denied)
            self.assertEqual(len(self.server.clients),1)
        finally:
            reader.feed_eof();await asyncio.wait_for(task,5)
        self.assertFalse(self.server.clients)
        # The same session identifier with a newer generation is still allowed;
        # unrelated generation-capacity limits are not bypassed by this check.
        result=await self.request(0,dict(op='open',member=1,door=1,session='capacity',generation=1))
        self.assertIn(b'"ok":true',result)
        self.assertFalse(self.server.clients);self.assertFalse(self.b.tickets)
    async def test_stalled_consumers_are_bounded_without_starving_issuer(self):
        self.b.capacity=1
        ready=asyncio.Event()
        class BlockedReader:
            async def readline(self):
                ready.set()
                await asyncio.Future()
        slow=Writer(40)
        task=asyncio.create_task(self.server.handle(BlockedReader(),slow))
        await asyncio.wait_for(ready.wait(),2)
        try:
            rejected=Writer(40)
            await self.server.handle(BlockedReader(),rejected)
            rejected.transport.abort.assert_called_once()
            self.assertEqual(len(self.server.consumers),1)
            reader,writer,issuer=await self.live_issuer('reserved-issuer')
            self.assertTrue(json.loads(writer.data)['ok'])
            reader.feed_eof();await asyncio.wait_for(issuer,2)
        finally:
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):await task
        self.assertFalse(self.server.clients or self.server.issuers or self.server.consumers)
    async def test_cancelled_handler_revokes_consumed_lease(self):
        reader,writer,task=await self.live_issuer('cancelled')
        try:
            ticket=json.loads(writer.data)['result']['ticket']
            lease=self.b.consume(40,1,'bbs',ticket)['lease']
        finally:
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):await asyncio.wait_for(task,5)
        with self.assertRaises(Denied):self.b.check(40,1,'bbs',lease)
        self.assertTrue(writer.closed);self.assertFalse(self.server.clients)
        self.assertFalse(self.b.sessions);self.assertFalse(self.b.leases)

    async def test_stalled_reply_revokes_lease_and_aborts_stalled_close(self):
        broker=self.b;consumed=[]
        class BlockedWriter(Writer):
            async def drain(self):
                if not consumed:
                    ticket=json.loads(self.data)['result']['ticket']
                    consumed.append(broker.consume(40,1,'bbs',ticket)['lease'])
                else:
                    # Revocation precedes even the attempted denial response.
                    self_test.assertFalse(broker.sessions)
                await asyncio.Future()
            async def wait_closed(self):await asyncio.Future()
        self_test=self
        reader=asyncio.StreamReader();writer=BlockedWriter(0)
        reader.feed_data(b'{"op":"open","member":1,"door":1,"session":"stalled","generation":0}\n')
        with patch('door_broker.WRITE_TIMEOUT',.02):
            await asyncio.wait_for(self.server.handle(reader,writer),2)
        self.assertTrue(writer.closed);writer.transport.abort.assert_called_once()
        self.assertFalse(self.server.clients);self.assertFalse(self.b.sessions)
        self.assertFalse(self.b.tickets);self.assertFalse(self.b.leases)
        with self.assertRaises(Denied):self.b.check(40,1,'bbs',consumed[0])
        result=await self.request(0,dict(op='open',member=1,door=1,session='recovered',generation=0))
        self.assertIn(b'"ok":true',result)

@unittest.skipUnless(sys.platform.startswith('sunos'),'Solaris UNIX peer, PTY and process contract')
class SolarisTicketPTY(unittest.IsolatedAsyncioTestCase):
    async def test_ticket_pipe_to_pty_then_issuer_disconnect(self):
        import contextlib
        import os
        import pty
        import select
        import signal
        import socket
        import subprocess
        import tempfile
        import time

        uid = os.getuid()
        self.assertGreater(uid,0)
        worker = r'''
import os, signal, sys

sys.path.insert(0,sys.argv[1])
from door_auth_client import AuthorizationError, DoorAuthorization

signal.alarm(12)
os.login_tty(0)
fd = int(os.environ.pop("LINER_AUTH_FD"))
with os.fdopen(fd,"rb") as ticket_pipe:
    line = ticket_pipe.read(512)
    assert line.endswith(b"\n") and line.count(b"\n") == 1
token = line[:-1].decode("ascii")
# Only a descriptor number was supplied through the environment.
assert token and all(token not in value for value in os.environ.values())
lease = DoorAuthorization(sys.argv[2],bbs="solaris-fixture",door=1,
                          ticket=token,broker_uid=os.getuid())
assert lease.authorize(7)["member"] == 7
print("LEASE LIVE",flush=True)
for raw in sys.stdin:
    if raw.strip() == "check":
        try:
            lease.authorize(7)
        except AuthorizationError:
            print("LEASE REVOKED",flush=True)
        else:
            print("LEASE LIVE",flush=True)
    elif raw.strip() == "quit":
        break
'''
        root = Path(__file__).resolve().parents[1]
        broker = Broker('solaris-fixture',{uid:[1]})
        consumer = Server(broker)
        # Only the test-owned socket pair gets an injected issuer identity.
        # Real peer lookup is used on both sides of the consumer connection.
        issuer = Server(broker,peer=lambda _:0)
        issuer_end, operator_end = socket.socketpair()
        reader, writer = await asyncio.open_connection(sock=issuer_end)
        issuer_task = asyncio.create_task(issuer.handle(reader,writer))
        operator_reader, operator = await asyncio.open_connection(sock=operator_end)

        def line_from_pty(fd, expected, token):
            deadline = time.monotonic()+3
            output = b''
            while len(output)<4096:
                remaining = deadline-time.monotonic()
                if remaining<=0 or not select.select([fd],[],[],remaining)[0]:
                    break
                chunk = os.read(fd,512)
                if not chunk:break
                output += chunk
                self.assertTrue(token not in output,'Credential appeared on the PTY')
                if expected in output.replace(b'\r\n',b'\n').split(b'\n')[:-1]:
                    return
            self.fail('Expected synthetic lease status did not arrive on the PTY')

        try:
            with tempfile.TemporaryDirectory(prefix='ln-door-broker-') as temporary:
                endpoint = str(Path(temporary)/'broker.sock')
                listener = await asyncio.start_unix_server(consumer.handle,path=endpoint)
                async with listener:
                    operator.write(b'{"op":"open","member":7,"door":1,"session":"pty-test","generation":0}\n')
                    await operator.drain()
                    issued = json.loads(await asyncio.wait_for(operator_reader.readline(),3))
                    self.assertIs(issued.get('ok'),True)
                    ticket = issued['result']['ticket']
                    encoded = ticket.encode('ascii')
                    with contextlib.ExitStack() as cleanup:
                        master, slave = pty.openpty()
                        cleanup.callback(os.close,master)
                        try:
                            ticket_read, ticket_write = os.pipe()
                            try:
                                try:
                                    os.write(ticket_write,encoded+b'\n')
                                finally:
                                    os.close(ticket_write)
                                argv = ['/usr/bin/ctrun','-l','contract','-o','noorphan',
                                        sys.executable,'-I','-c',worker,str(root/'scripts'),endpoint]
                                process = subprocess.Popen(
                                    argv,stdin=slave,stdout=slave,stderr=slave,pass_fds=(ticket_read,),
                                    start_new_session=True,env={'PATH':'/usr/bin','LC_ALL':'C',
                                                                'LINER_AUTH_FD':str(ticket_read)})
                            finally:
                                os.close(ticket_read)
                        finally:
                            os.close(slave)
                        try:
                            await asyncio.to_thread(line_from_pty,master,b'LEASE LIVE',encoded)
                            with self.assertRaises(AuthorizationError):
                                await asyncio.to_thread(door_auth_client.request,endpoint,'consume',
                                                        bbs='solaris-fixture',door=1,token=ticket,
                                                        broker_uid=uid)
                            operator.close()
                            await asyncio.wait_for(issuer_task,3)
                            self.assertFalse(broker.leases or broker.sessions or broker.tickets)
                            os.write(master,b'check\n')
                            await asyncio.to_thread(line_from_pty,master,b'LEASE REVOKED',encoded)
                            os.write(master,b'quit\n')
                            self.assertEqual(await asyncio.to_thread(process.wait,3),0)
                        finally:
                            if process.poll() is None:
                                process.send_signal(signal.SIGKILL)
                                await asyncio.to_thread(process.wait,3)
        finally:
            operator.close()
            await asyncio.wait_for(issuer_task,3)
            await operator.wait_closed()
        print('SOLARIS_BROKER_PTY ticket FD, one-use, issuer EOF revocation',flush=True)


if __name__=='__main__':unittest.main()
