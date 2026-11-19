"""Game-server consumer API. Call check again for each authoritative operation.

A game server runs under its configured consumer UID; a Door has no issuer API.
The Door sends its one-use credential to that server over a protected channel.
"""
import json,socket

class AuthorizationError(RuntimeError):pass

def request(path,operation,*,bbs,door,token,timeout=3):
    if operation not in ('consume','check'):raise ValueError('consumer operation required')
    payload=json.dumps(dict(op=operation,bbs=bbs,door=door,token=token),separators=(',',':')).encode()+b'\n'
    if len(payload)>4096:raise AuthorizationError('authorization unavailable')
    try:
        with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as connection:
            connection.settimeout(timeout);connection.connect(path);connection.sendall(payload)
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
