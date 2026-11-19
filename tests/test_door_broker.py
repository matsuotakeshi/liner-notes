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

class Consumer(unittest.TestCase):
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
    async def test_live_issuer_owns_lease(self):
        reader,writer,task=await self.live_issuer('s')
        token=json.loads(writer.data)['result']['ticket']
        result=json.loads(await self.request(40,dict(op='consume',door=1,bbs='bbs',token=token)))['result']
        self.assertEqual(result['claims']['member'],1)
        reader.feed_eof();await task
        check=await self.request(40,dict(op='check',door=1,bbs='bbs',token=result['lease']))
        self.assertIn(b'"ok":false',check)
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
        payloads=[b'x'*4097+b'\n',b'{"secret":"do-not-echo"}',b'[]\n',b'null\n',b'\xff\n']
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

if __name__=='__main__':unittest.main()
