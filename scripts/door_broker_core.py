"""Transport-independent, fail-closed one-use Door authorization state.

Only the authenticated issuer adapter may call open/issue/close. Games have
consume/check only. No database, passwords, signing keys or game logic here.
"""
from dataclasses import dataclass
import hashlib,re,secrets,time

class Denied(ValueError):
    pass

def positive(value,maximum=2**63-1):
    if type(value) is not int or not 0<value<=maximum:raise Denied('invalid request')
    return value

def auth_generation(value):
    # Existing moderation_epoch starts at zero; zero is not unauthenticated.
    if type(value) is not int or not 0<=value<=0xffffffff:raise Denied('invalid generation')
    return value

def identity(value):
    if not isinstance(value,str) or not re.fullmatch(r'[A-Za-z0-9_.-]{1,64}',value):raise Denied('invalid request')
    return value

def digest(token):
    if not isinstance(token,str) or not re.fullmatch('[0-9a-f]{64}',token):raise Denied('authorization denied')
    return hashlib.sha256(token.encode('ascii')).digest()

@dataclass(eq=False)
class Session:
    bbs: str
    member: int
    door: int
    session: str
    generation: int
    deadline: float
    active: bool=True
    replay_key: tuple | None=None
    consumed: bool=False

class Broker:
    def __init__(self,bbs,consumers,*,clock=time.monotonic,ttl=30,session_ttl=3600,capacity=4096):
        self.bbs=identity(bbs);self.clock=clock
        self.ttl=positive(ttl,120);self.session_ttl=positive(session_ttl,86400)
        self.capacity=positive(capacity,65536)
        self.consumers={positive(uid):frozenset(positive(d,86400) for d in doors) for uid,doors in consumers.items()}
        self.sessions={};self.generations={};self.tickets={};self.leases={}

    def sweep(self):
        now=self.clock()
        for session in list(self.sessions.values()):
            if now>=session.deadline:self.close(session)
        self.tickets={key:value for key,value in self.tickets.items() if value[0].active and now<value[1]}
        self.leases={key:value for key,value in self.leases.items() if value[0].active}
        self.generations={key:value for key,value in self.generations.items() if now<value[1]}

    def open(self,member,door,session,generation,*,scope=None,sequence=None):
        member=positive(member,999999);door=positive(door,86400);session=identity(session);generation=auth_generation(generation)
        self.sweep()
        # Legacy issuers retain session/generation replay semantics. The current
        # supervisor supplies a stable incarnation+slot scope and a parent-owned
        # monotonic launch sequence, while public claims keep the BBS identity.
        if scope is None and sequence is None:
            replay_key=('legacy',session);version=generation
        elif scope is not None and sequence is not None:
            replay_key=('slot',identity(scope));version=positive(sequence)
        else:raise Denied('invalid replay scope')
        previous=self.generations.get(replay_key)
        if previous and version<=previous[0]:raise Denied('stale generation')
        if replay_key not in self.generations and len(self.generations)>=self.capacity:raise Denied('capacity reached')
        old=self.sessions.get(session)
        if old and old.replay_key!=replay_key:raise Denied('session scope collision')
        if any(s.active and s.replay_key==replay_key and s.session!=session for s in self.sessions.values()):raise Denied('slot still active')
        if old is None and len(self.sessions)>=self.capacity:raise Denied('capacity reached')
        if not any(door in doors for doors in self.consumers.values()):raise Denied('door not authorized')
        if old:self.close(old)
        active=Session(self.bbs,member,door,session,generation,self.clock()+self.session_ttl)
        active.replay_key=replay_key
        self.sessions[session]=active;self.generations[replay_key]=(version,active.deadline)
        return active

    def close(self,session):
        session.active=False
        if self.sessions.get(session.session) is session:del self.sessions[session.session]
        self.tickets={key:value for key,value in self.tickets.items() if value[0] is not session}
        self.leases={key:value for key,value in self.leases.items() if value[0] is not session}

    def issue(self,session):
        self.sweep()
        if not session.active or self.sessions.get(session.session) is not session:raise Denied('inactive session')
        # One unconsumed ticket per session; repeated issuance invalidates the old one.
        self.tickets={key:value for key,value in self.tickets.items() if value[0] is not session}
        session.consumed=False
        token=secrets.token_hex(32)
        self.tickets[digest(token)]=(session,min(self.clock()+self.ttl,session.deadline),secrets.token_hex(16))
        return token

    def ready(self,session):
        """Only the issuer owning this session may ask if its ticket was consumed."""
        if (not session.active or self.sessions.get(session.session) is not session or
            self.clock()>=session.deadline):
            raise Denied('inactive session')
        return session.consumed

    def authorize_consumer(self,uid,door,bbs):
        if bbs!=self.bbs or door not in self.consumers.get(uid,()):raise Denied('authorization denied')

    def claims(self,session,ticket_id):
        return dict(bbs=session.bbs,member=session.member,door=session.door,
                    session=session.session,generation=session.generation,ticket_id=ticket_id,
                    expires_in=max(0,int(session.deadline-self.clock())))

    def consume(self,uid,door,bbs,token):
        self.authorize_consumer(uid,door,bbs);self.sweep();key=digest(token)
        item=self.tickets.get(key)
        if not item or item[0].door!=door:raise Denied('authorization denied')
        if len(self.leases)>=self.capacity:raise Denied('capacity reached')
        session,_,ticket_id=self.tickets.pop(key)
        lease=secrets.token_hex(32);self.leases[digest(lease)]=(session,uid,ticket_id)
        session.consumed=True
        return dict(lease=lease,claims=self.claims(session,ticket_id))

    def check(self,uid,door,bbs,lease):
        self.authorize_consumer(uid,door,bbs);self.sweep();item=self.leases.get(digest(lease))
        if not item or item[1]!=uid or item[0].door!=door:raise Denied('authorization denied')
        return self.claims(item[0],item[2])
