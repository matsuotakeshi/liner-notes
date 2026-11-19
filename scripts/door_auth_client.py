"""Game-server consumer API. Call check again for each authoritative operation.

A game server runs under its configured consumer UID; a Door has no issuer API.
The Door sends its one-use credential to that server over a protected channel.
"""
import json,socket
from door_broker import peer_uid

class AuthorizationError(RuntimeError):pass

def request(path,operation,*,bbs,door,token,broker_uid,timeout=3):
    if operation not in ('consume','check'):raise ValueError('consumer operation required')
    if type(broker_uid) is not int or broker_uid<=0:raise ValueError('dedicated Broker UID required')
    payload=json.dumps(dict(op=operation,bbs=bbs,door=door,token=token),separators=(',',':')).encode()+b'\n'
    if len(payload)>4096:raise AuthorizationError('authorization unavailable')
    try:
        with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as connection:
            connection.settimeout(timeout);connection.connect(path)
            if peer_uid(connection)!=broker_uid:raise AuthorizationError('Broker identity mismatch')
            connection.sendall(payload)
            data=bytearray()
            while len(data)<=4096:
                chunk=connection.recv(1)
                if not chunk:raise AuthorizationError('authorization unavailable')
                if chunk==b'\n':break
                data+=chunk
            else:raise AuthorizationError('authorization unavailable')
            result=json.loads(data)
            if result.get('ok') is not True:raise AuthorizationError('authorization denied')
            return result['result']
    except (OSError,ValueError,KeyError) as error:
        raise AuthorizationError('authorization unavailable') from None

class DoorAuthorization:
    """A game-server-side lease; caller-selected player IDs never grant authority."""
    def __init__(self,path,*,bbs,door,ticket,broker_uid):
        self.path=path;self.bbs=bbs;self.door=door;self.broker_uid=broker_uid
        result=request(path,'consume',bbs=bbs,door=door,token=ticket,broker_uid=broker_uid)
        self._lease=result['lease'];claims=result['claims']
        self._identity=tuple(claims[key] for key in ('bbs','member','door','session','generation','ticket_id'))
        if claims['bbs']!=bbs or claims['door']!=door:raise AuthorizationError('authorization binding mismatch')

    def authorize(self,member=None):
        claims=request(self.path,'check',bbs=self.bbs,door=self.door,token=self._lease,broker_uid=self.broker_uid)
        if tuple(claims[key] for key in ('bbs','member','door','session','generation','ticket_id'))!=self._identity:
            raise AuthorizationError('authorization binding mismatch')
        if member is not None and (type(member) is not int or member!=claims['member']):
            raise AuthorizationError('different member denied')
        return claims
