"""HTTP boundary for the BBS connector. BBS files are never opened by this service."""
import asyncio
import base64
from collections import deque
from dataclasses import dataclass, field
import hashlib
import json
import logging
import os
from pathlib import Path
import re
import secrets
import tempfile
import time
from urllib.parse import urlsplit, quote

from aiohttp import web, WSMsgType
from .bridge import Bridge, BridgeError, empty_view, response_data
from .commands import COMMANDS, CORE_UPDATES, RUNTIME_UPDATES, SESSION_UPDATES, action_arguments, core_update, visible
from .presentation import Presentation
from .terminal import relay
from .transfers import ResumeRecords

ROOT = Path(__file__).resolve().parent
LOG = logging.getLogger(__name__)


class TemplateError(Exception):
    pass


@dataclass
class WebSession:
    bridge: Bridge
    guest_lock: asyncio.Lock = field(default_factory=asyncio.Lock)
    member: int = 0
    csrf: str = field(default_factory=lambda:secrets.token_urlsafe(32))
    events: deque = field(default_factory=lambda:deque(maxlen=100))
    terminal: bool = False
    terminal_ws: object = None
    downloads: dict = field(default_factory=dict)
    vault_sigop: bool = False
    games: list = field(default_factory=list)
    secret_mode: int = 0
    secret_challenge: str = ''


def create_app(config):
    cfg = config['web']; upstream = config['bbs']
    origin = cfg['origin'].rstrip('/')
    parsed = urlsplit(origin)
    if parsed.scheme not in ('http','https') or not parsed.netloc or parsed.path or parsed.query or parsed.fragment:
        raise ValueError('origin must be an exact http(s) origin')
    base = cfg.get('base_path','/notes/')
    if not re.fullmatch(r'/(?:[A-Za-z0-9_-]+/)*',base): raise ValueError('Invalid base_path')
    secure = cfg.get('secure_cookies',True)
    if not secure and cfg.get('listen','127.0.0.1') not in ('127.0.0.1','::1'): raise ValueError('Insecure development mode must bind loopback')
    cookie = 'liner_notes_web_'+hashlib.sha256(base.encode()).hexdigest()[:12]
    presentation = Presentation(cfg.get('messages',str(ROOT/'messages')),cfg.get('timezone','Asia/Tokyo'))
    sessions = {}
    opening = asyncio.Lock()
    spool = Path(cfg['spool']);spool.mkdir(mode=0o700,parents=True,exist_ok=True)
    if spool.is_symlink(): raise ValueError('Spool directory must not be a symlink')
    upload_limit = min(int(cfg.get('max_upload_bytes',1073741824)),1073741824)
    spool_limit = int(cfg.get('max_spool_bytes',17179869184))
    transfers = {}
    transfer_tasks=set()
    resumes = ResumeRecords(spool,upstream)
    transfer_timeout=max(30,min(600,int(upstream.get("transfer_timeout",120))))

    @web.middleware
    async def boundary(request, handler):
        if request.host != parsed.netloc: raise web.HTTPForbidden(text='Unexpected Host')
        if request.method not in ('GET','HEAD') or request.path.endswith(('/events','/terminal')):
            if request.headers.get('Origin') != origin: raise web.HTTPForbidden(text='Origin rejected')
        try:
            response = await handler(request)
        except BridgeError as error:
            response = web.json_response({'error':str(error)},status=409)
        except TemplateError:
            LOG.exception('Web template could not be loaded')
            response = web.json_response({'error':'Web template unavailable; check template files and permissions'},status=500)
        except (ValueError, KeyError, TypeError, UnicodeError) as error:
            response = web.json_response({'error':'Invalid request: '+str(error)[:160]},status=400)
        except (ConnectionError, asyncio.TimeoutError):
            response = web.json_response({'error':'BBS connection unavailable'},status=503)
        except OSError:
            LOG.exception('Web file operation failed')
            response = web.json_response({'error':'Web file access failed; check service logs and permissions'},status=500)
        response.headers['X-Content-Type-Options']='nosniff'
        response.headers['Referrer-Policy']='same-origin'
        response.headers['Content-Security-Policy']="default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'"
        response.headers['Cache-Control']='no-store'
        return response

    @web.middleware
    async def identity_boundary(request, handler):
        ident = request.cookies.get(cookie, '')
        session = sessions.get(ident)
        if not session or not request.path.startswith(base + 'api/'):
            return await handler(request)
        if request.path in (base+'api/terminal', base+'api/events'):
            # The event stream checks its admission cookie for its whole lifetime.
            # The terminal has its own BBS connection and login.
            return await handler(request)
        if session.member:
            # An interactive logout retires the bridge along with its outstanding work.
            try:
                if session.bridge.state.get('user') != session.member:
                    raise web.HTTPUnauthorized(text='Session expired')
                reply = await handler(request)
                member = session.bridge.state.get('user')
                if member and member != session.member:
                    raise web.HTTPUnauthorized(text='Session changed')
                return reply
            finally:
                if session.bridge.state.get('user') != session.member and sessions.pop(ident, None):
                    if session.terminal_ws is not None and session.terminal_ws.prepared:
                        await session.terminal_ws.close()
                    await session.bridge.close()

        async with session.guest_lock:
            if sessions.get(ident) is not session:
                raise web.HTTPUnauthorized(text='Session expired')
            try:
                reply = await handler(request)
                member = session.bridge.state.get('user')
                if not member:
                    return reply
                # Any guest dialogue can finish authentication, including a
                # detailed application or password prompt. Rotate before replying.
                payload = json.loads(reply.body)
                if payload.get('state', {}).get('user') != member:
                    raise BridgeError('Sign-in result unavailable; please reconnect')
                async with session.bridge.lock:
                    games = await session.bridge.exchange('GAMES')
                if sessions.get(ident) is not session or session.bridge.state.get('user') != member:
                    raise web.HTTPUnauthorized(text='Session expired')
                session.games = [{'id':int(m[1]), 'title':m[2].rstrip('\r')}
                                 for m in re.finditer(r'^GAME (\d+) \d+ (.*)\r?$', games['text'], re.M)]
                if session.terminal_ws is not None and session.terminal_ws.prepared:
                    await session.terminal_ws.close()
                if sessions.get(ident) is not session:
                    raise web.HTTPUnauthorized(text='Session expired')
                session.downloads.clear()
                session.member = member
                session.csrf = secrets.token_urlsafe(32)
                payload['csrf'] = session.csrf
                payload['state']['games'] = session.games
                result = web.json_response(payload)
                new_id = secrets.token_urlsafe(32)
                result.set_cookie(cookie,new_id,httponly=True,secure=secure,samesite='Strict',path=base)
                del sessions[ident]
                sessions[new_id] = session
                return result
            except BaseException as error:
                if session.bridge.state.get('user') or isinstance(error, asyncio.CancelledError):
                    sessions.pop(ident, None)
                    await session.bridge.close()
                raise

    app = web.Application(middlewares=[boundary, identity_boundary],client_max_size=400000)
    app['sessions']=sessions

    async def link_control(request):
        session=current(request,mutate=request.method=='POST')
        if any(session.bridge.state.get(k) for k in ('editing','secret','chat','gather','input','confirmation','talk','door','binary')):raise BridgeError('Finish the current dialogue before managing links')
        await session.bridge.command('PING')
        if not session.bridge.state.get('admin'):raise web.HTTPForbidden(text='Administrator required')
        path=cfg.get('link_control_socket')
        if not path:raise web.HTTPNotFound(text='Link management is not configured')
        body=await object_body(request) if request.method=='POST' else {'action':'status'}
        if set(body)-{'action','peer'} or body.get('action') not in ('status','pause','resume','retry'):raise ValueError('Unknown link action')
        if body['action']!='status' and not re.fullmatch('[0-9a-f]{64}',str(body.get('peer',''))):raise ValueError('Invalid peer identity')
        async with asyncio.timeout(8):
            reader,writer=await asyncio.open_unix_connection(path,limit=1048576)
            try:
                writer.write(json.dumps(body).encode()+b'\n');await writer.drain()
                raw=await reader.readline()
                if len(raw)>1048576:raise ValueError('Link status too large')
                result=json.loads(raw)
                if 'error' in result:raise ValueError(result['error'])
                return web.json_response(result)
            finally:writer.close();await writer.wait_closed()

    async def object_body(request):
        value=await request.json()
        if not isinstance(value,dict):raise ValueError('Expected a JSON object')
        return value

    def current(request, *, mutate=False, terminal_ok=False):
        session = sessions.get(request.cookies.get(cookie,''))
        if not session: raise web.HTTPUnauthorized(text='Session expired')
        if session.bridge.closed: raise BridgeError('BBS connection closed; please reconnect')
        if mutate and not secrets.compare_digest(request.headers.get('X-CSRF-Token',''),session.csrf):
            raise web.HTTPForbidden(text='CSRF token rejected')
        session.bridge.last_browser=time.monotonic()
        return session

    def pack(session, result=None):
        result = result or {'text':'','state':dict(session.bridge.state)}
        result['state']['vault_sigop']=session.vault_sigop
        result['state']['games']=session.games
        result['state']['link_management']=bool(cfg.get('link_control_socket')) and bool(result['state'].get('admin'))
        result['data']={**empty_view(),**result['data']} if 'data' in result else response_data(result['text'])
        mode=result['state'].get('password_mode',0) if result['state'].get('secret') else 0
        if mode!=session.secret_mode:
            session.secret_mode=mode
            session.secret_challenge=''
        if mode and not session.secret_challenge:
            session.secret_challenge=secrets.token_urlsafe(24)
        result['secret_challenge']=session.secret_challenge
        result['csrf']=session.csrf
        result['actions']=[{k:v for k,v in item.items() if k!='command'} for item in COMMANDS.values() if visible(item,result['state'])]
        return result

    def response(session,result=None):return web.json_response(pack(session,result))

    async def new_session(request):
        async with opening:
            existing=sessions.get(request.cookies.get(cookie,''))
            if existing and not existing.bridge.closed:
                if not secrets.compare_digest(request.headers.get('X-CSRF-Token',''),existing.csrf):raise web.HTTPForbidden()
                return response(existing)
            if len(sessions)>=cfg.get('max_sessions',64):raise web.HTTPServiceUnavailable(text='Web session capacity reached')
            bridge=await Bridge(upstream['host'],int(upstream['port']),telnet=upstream.get('telnet',True),timeout=upstream.get('timeout',30)).open()
            session=WebSession(bridge);ident=secrets.token_urlsafe(32);sessions[ident]=session
        result=response(session)
        result.set_cookie(cookie,ident,httponly=True,secure=secure,samesite='Strict',path=base)
        return result

    async def session_info(request):
        session=current(request,terminal_ok=True)
        async with session.bridge.lock:
            return response(session)

    async def logout(request):
        session=current(request,mutate=True,terminal_ok=True)
        sessions.pop(request.cookies.get(cookie,''),None)
        if session.terminal_ws:await session.terminal_ws.close()
        await session.bridge.close()
        result=web.json_response({'ok':True});result.del_cookie(cookie,path=base);return result

    async def authenticate(request):
        session=current(request,mutate=True);body=await object_body(request)
        if any(session.bridge.state.get(k) for k in ('user','editing','secret','chat','gather','input')):raise BridgeError('Finish the current dialogue or sign out before signing in')
        login=body.get('login','');password=body.get('password','')
        if not isinstance(login,str) or not re.fullmatch(r'[a-z0-9_-]{1,32}',login):raise BridgeError('Invalid login name')
        if not isinstance(password,str) or not 8<=len(password.encode())<=128 or any(ord(c)<32 or ord(c)==127 for c in password):raise BridgeError('Password must be 8–128 bytes without controls')
        async with session.bridge.lock:
            result=await session.bridge.exchange(('REGISTER ' if body.get('register') else 'LOGIN ')+login)
            if session.bridge.state.get('secret'):
                result=await session.bridge.exchange(password)
            # Detailed application forms continue through the interactive form.
        return response(session,result)

    # Typed result lists share the action form and return the Core's opaque next position.
    discovery = {
        'board.news': ('board_news','{date}','NEWSFROM {cursor} {date}'),
        'mail.news': ('mail_news','{date}','MAIL NEWSFROM {cursor} {date}'),
        'mail.inbox': ('mail_inbox','','MAIL +'),
        'mail.sent': ('mail_sent','','MAIL +'),
        'mail.unread': ('mail_unread','','MAIL +'),
        'file.boards': ('file_boards','','FILEBOARDS {cursor}'),
        'file.list': ('files','','FILES {cursor}'),
        'file.subtree': ('files','{path}','FILESINFROM {cursor} {path}'),
        'file.search': ('file_search','{query}','FILESEARCHFROM {cursor} {query}'),
        'file.search_in': ('file_search','{path}|{query}','FILESEARCHFROM {cursor} {path}|{query}'),
        'file.news': ('file_news','{path}|{date}','FILENEWSFROM {cursor} {path}|{date}'),
        'profile.list': ('profiles','*','PROFILESFROM {cursor} *'),
        'profile.search': ('profiles','{query}','PROFILESFROM {cursor} {query}'),
        'profile.find': ('profile_find','{field} {query}','PROFILESFROM {cursor} {query}'),
        'profile.group': ('profile_group','','PROFILESFROM {cursor} *'),
    }
    extra_views = {
        'board.unread': ('article_unread','','UNREAD 0 {cursor}','board'),
        'profile.self': ('profile_login','','PROFILE',''),
        'profile.view': ('profile_login','{login}','PROFILE {login}',''),
        'settings.prefs': ('preferences','','PREFS',''),
        'admin.application': ('application','','APPLICATION {user}','user'),
        'admin.connections': ('connections','','CONNECTIONS',''),
        'board.new': ('article_new','','NEW','board'),
        'board.prev': ('article_prev','','PREV','article'),
        'board.next': ('article_next','','NEXT','article'),
        'board.parent': ('article_parent','','PARENT','article'),
        'board.child': ('article_child','','CHILD','article'),
        'board.scan': ('article_scan','{order} {scope} {author} {query}','SCAN {order} {scope} {author} {query}','board'),
        'mail.scan': ('mail_scan','{order}','MAIL SCAN {order}',''),
        'mail.prev': ('mail_previous','','MAIL P','mail_current'),
        'mail.next': ('mail_next','','MAIL N',''),
        'mail.repeat': ('mail_read','','MAIL L','mail_current'),
        'mail.attachments': ('mail_files','','MAIL FILES {cursor}',''),
        'file.children': ('file_children','{path}','FILECHILDREN {path}',''),
        'file.detail': ('file_detail','','FILE {id}','id'),
        'file.text': ('file_text','','FILETYPE {id} {cursor}','id'),
        'file.audit': ('file_audit','','FILEAUDIT {cursor}',''),
        'file.history': ('file_history','','FILEHISTORY 0 {cursor}',''),
        'file.stats': ('file_stats','','FILESTATS 0 {cursor}',''),
        'admin.audit': ('article_audit','','AUDIT {cursor}',''),
        'admin.audit_article': ('article_audit','','AUDIT {cursor} ARTICLE {id}','id'),
        'admin.audit_actor': ('article_audit','{login}','AUDIT {cursor} ACTOR {login}',''),
        'admin.members': ('member_list','','MEMBERS {cursor}',''),
        'admin.applications': ('applications','','APPLICATIONS {cursor}',''),
    }
    search_actions = ('board.search','board.author','mail.search')
    check_start = {'board.check':'ARTICLES {scope}','mail.check':'MAIL','file.check':'FILES {path}'}
    check_control = {'check.next':'check_next','check.done':'check_done','check.cancel':'check_cancel'}
    session_views = {'chat.online':'chat_online','chat.room':'chat_room','chat.preferences':'chat_prefs',
                     'chat.status':'chat_status','online':'chat_online','file.batch_list':'batch_list',
                     'board.resume':'article_draft','mail.resume':'mail_draft'}
    upload_views = {'file.upload_status':'upload_status','mail.upload_status':'mail_upload'}
    connection_history = {'userlog':'userlog','userstats':'userstats',
                          'admin.userlog':'userlog','admin.userstats':'userstats'}

    def search_positions(payload, state):
        cursor=payload.get('cursor',0)
        board=payload.get('search_board',state.get('board',0))
        for position in (cursor,board):
            if type(position) is not int or not 0<=position<=2147483646:
                raise ValueError('Invalid search position')
        return cursor,board

    async def operate(request):
        session=current(request,mutate=True);payload=await object_body(request)
        key=payload['action']
        async with session.bridge.lock:
            args,item=action_arguments(key,payload.get('values',{}),{**session.bridge.state,'vault_sigop':session.vault_sigop},payload.get('confirmed') is True)
            group=item['group']
            if session.bridge.state.get('chat') and group!='chat':raise BridgeError('Leave the chat before this operation')
            state=session.bridge.state
            if state.get('secret'):raise BridgeError('Finish or cancel the current password prompt first')
            if state.get('core_runtime_controls')==1:
                if key=='usertable':
                    cursor,_=search_positions(payload,state)
                    return response(session,await session.bridge.view('usertable',0,cursor))
                if key in RUNTIME_UPDATES:
                    return response(session,await session.bridge.update(key,flag=item['confirm'],**core_update(key,args)))
            if state.get('core_session_controls')==1:
                if key in session_views:
                    return response(session,await session.bridge.view(session_views[key],0,0))
                if key in connection_history:
                    cursor,_=search_positions(payload,state)
                    return response(session,await session.bridge.view(connection_history[key],0,cursor,
                                                                  'ALL' if key.startswith('admin.') else ''))
                if key in upload_views:
                    ident=args['id']
                    if not re.fullmatch(r'[0-9]{1,10}',ident) or not 1<=int(ident)<=2147483646:
                        raise ValueError('Invalid upload ID')
                    return response(session,await session.bridge.view(upload_views[key],int(ident),0))
                if key in SESSION_UPDATES:
                    fields=core_update(key,args)
                    result=await session.bridge.update(key,flag=item['confirm'],**fields)
                    if key in ('file.upload_cancel','mail.upload_cancel') and result['mutation']['ok']:
                        try:
                            resumes.remove(session.bridge.state,'mail' if key.startswith('mail.') else 'public',fields['id'])
                        except OSError:
                            LOG.warning('Upload cancelled but its Web resume record could not be removed')
                    return response(session,result)
            typed = state.get('core_updates') == 1 and key in CORE_UPDATES
            if typed and (key!='profile.handle' or state.get('user')):
                return response(session,await session.bridge.update(key,flag=item['confirm'],**core_update(key,args)))
            if state.get('mail') and group!='mail':await session.bridge.exchange('MAIL EXIT')
            command=item['command'].format(**args).rstrip()
            if session.bridge.state.get('core_operations')==1:
                if key in check_start:
                    _,board=search_positions(payload,session.bridge.state)
                    scope=check_start[key].format(**args)
                    return response(session,await session.bridge.view('check_start',board,0,scope))
                if key in check_control:
                    return response(session,await session.bridge.view(check_control[key],0,0))
            if key in extra_views:
                state=session.bridge.state
                cursor,board=search_positions(payload,state)
                kind,path,continuation,source=extra_views[key]
                if state.get('core_operations')==1:
                    if source in ('id','user'):
                        value=args[source]
                        if not re.fullmatch(r'[0-9]{1,10}',value) or not 1<=int(value)<=2147483646:
                            raise ValueError('Invalid target ID')
                        ident=int(value)
                    else:
                        ident=board if source=='board' else state.get(source,0)
                    return response(session,await session.bridge.view(kind,ident,cursor,path.format(**args)))
                if cursor:command=continuation.format(cursor=cursor,**args)
            elif key in discovery:
                cursor,board=search_positions(payload,session.bridge.state)
                kind,parameters,continuation=discovery[key]
                if session.bridge.state.get('core_discovery')==1:
                    ident=board if kind=='board_news' else 0
                    if kind=='profile_group':
                        group_id=args['group']
                        if not re.fullmatch(r'[0-9]{1,3}',group_id) or not 1<=int(group_id)<=100:
                            raise ValueError('Invalid member group')
                        ident=int(group_id)
                    if kind=='profile_find':
                        args['field']=args['field'].upper()
                    return response(session,await session.bridge.view(kind,ident,cursor,parameters.format(**args).rstrip()))
                if cursor:command=continuation.format(cursor=cursor,**args)
            elif key in search_actions:
                cursor,board=search_positions(payload,session.bridge.state)
                if key=='mail.search':
                    query=args['query'].strip()
                    kind,board,parameters='mail_search',0,query
                    if cursor:command=f'MAIL SEARCHFROM {cursor} {query}'
                else:
                    scope=args['scope'].upper()
                    author=args.get('author','*').strip()
                    query=args.get('query','*').strip()
                    if scope not in ('HERE','SUBTREE','ALL') or not 0<len(author.encode())<=32 or any(c.isspace() for c in author):
                        raise ValueError('Invalid article search filter')
                    kind,parameters='find',f'{scope} {author} {query}'
                    command=f'FINDFROM {cursor} {parameters}' if cursor else f'FIND {parameters}'
                if not 0<len(query.encode())<=80:
                    raise ValueError('Search text must be 1 to 80 bytes')
                if session.bridge.state.get('core_search')==1:
                    return response(session,await session.bridge.view(kind,board,cursor,parameters))
            result=await session.bridge.exchange(command)
            if item['edit'] and session.bridge.state.get('editing'):
                preview=await session.bridge.exchange('/PREVIEW');result['preview']=preview['text']
        return response(session,result)

    async def file_role(session,path):
        # Display hint only. The core still authorizes every management command.
        # Read fixed protocol headers, not the freely editable board guide.
        session.vault_sigop=False
        if not session.bridge.state.get('user'):return
        path='' if path=='-' else path
        while True:
            info=await session.bridge.exchange('FILEBOARDINFO '+(path or '-'))
            pattern=r'^FILEBOARDSETTING [^\r\n]+\r\nDISPLAY [^\r\n]*\r\nSIGOP legacy-member:'+str(session.bridge.state['user'])+r'\r\n'
            if re.match(pattern,info['text']):session.vault_sigop=True;return
            if not path:return
            path=path.rsplit('/',1)[0] if '/' in path else ''

    async def query(request):
        session=current(request,mutate=True);payload=await object_body(request);kind=payload['kind']
        # Explicit operations; all of these still pass the core's ACL checks.
        number=int(payload.get('id',1));cursor=int(payload.get('cursor',0 if kind in ('mail','tree') else 1))
        if not 0<=number<=2147483646 or not 0<=cursor<=2147483646:raise ValueError('Invalid number')
        commands={'boards':f'BOARDS {max(cursor,1)}','notes':f'NOTES {max(cursor,1)}','read':f'READ {number}',
                  'mail':'MAIL +' if cursor else 'MAIL LIST','mail_read':f'MAIL READ {number}','files':f'FILES {max(cursor,1)}',
                  'profile':f'PROFILEID {number}','tree':f'TREE {number}'+(f' {cursor}' if cursor else ''),
                  'readglobal':f'READGLOBAL {number}'}
        if kind not in commands:raise ValueError('Invalid view')
        path = ''
        if kind=='files' and payload.get('path'):
            path=payload['path']
            if not isinstance(path,str) or len(path.encode())>48 or any(ord(c)<32 for c in path) or '|' in path:raise ValueError('Invalid file board')
            commands[kind]=f'FILESINFROM {max(cursor,1)} {path}'
        async with session.bridge.lock:
            if session.bridge.state.get('editing') or session.bridge.state.get('secret') or session.bridge.state.get('chat') or session.bridge.state.get('gather'):
                raise BridgeError('Finish or cancel the current dialogue before changing views')
            if payload.get('board'):
                selected=await session.bridge.exchange('BOARD '+str(int(payload['board'])))
                if 'ERR ' in selected['text'] or session.bridge.state.get('secret'):return response(session,selected)
            if session.bridge.state.get('mail') and kind not in ('mail','mail_read'):
                await session.bridge.exchange('MAIL EXIT')
            if session.bridge.state.get('core_views') == 1:
                result=await session.bridge.view(kind,number,cursor,path)
            else:
                result=await session.bridge.exchange(commands[kind])
            if kind=='files':await file_role(session,payload.get('path','-'))
        return response(session,result)

    async def input_line(request):
        session=current(request,mutate=True);payload=await object_body(request);line=payload.get('line','')
        if not isinstance(line,str):raise ValueError('Invalid input')
        # The Web console is for line interaction. Passwords have a separate
        # endpoint so an obsolete form cannot become an ordinary BBS command.
        async with session.bridge.lock:
            if session.bridge.state.get('secret'):raise BridgeError('Use the password prompt')
            in_text=any(session.bridge.state.get(k) for k in ('editing','chat','gather'))
            if not in_text and re.match(r'(?i)\s*(CONNECTOR|RAW|MENU|QUIT|TRANSFER|DOOR|Z(?:UPLOAD|DOWNLOAD|RESUME|MAIL)[A-Z]*|(?:MAIL)?(?:UPLOAD|DOWNLOAD)[A-Z]*)\b',line):
                raise BridgeError('Use the Web transfer controls or the terminal for this command')
            return response(session,await session.bridge.exchange(line))

    async def secret_input(request):
        session=current(request,mutate=True);payload=await object_body(request)
        challenge=payload.get('challenge')
        cancel=payload.get('cancel',False)
        password=payload.get('password','')
        if not isinstance(challenge,str) or not re.fullmatch(r'[A-Za-z0-9_-]{32}',challenge):
            raise ValueError('Invalid password prompt')
        if type(cancel) is not bool or (cancel and password) or not isinstance(password,str):
            raise ValueError('Invalid password input')
        if not cancel and (not 8<=len(password.encode('utf-8'))<=128 or any(ord(c)<32 or ord(c)==127 for c in password)):
            raise ValueError('Password must be 8–128 bytes without controls')
        async with session.bridge.lock:
            state=session.bridge.state
            if (not state.get('secret') or not session.secret_mode or state.get('password_mode')!=session.secret_mode
                    or not secrets.compare_digest(challenge,session.secret_challenge)):
                raise BridgeError('Password prompt expired')
            session.secret_challenge=''
            result=await session.bridge.exchange('/CANCEL' if cancel else password)
            return response(session,result)

    async def conference(request):
        session = current(request, mutate=True)
        payload = await object_body(request)
        action = payload.get('action')
        topic = payload.get('topic', False)
        if type(topic) is not bool:
            raise ValueError('Invalid conference type')
        room, cursor = payload.get('room', 0), payload.get('cursor', 0)
        if (type(room) is not int or not 0 <= room <= 1500
                or type(cursor) is not int or not 0 <= cursor <= 2147483646):
            raise ValueError('Invalid conference room or position')
        if topic and room:
            raise ValueError('Invalid topic room')
        async with session.bridge.lock:
            state = session.bridge.state
            if not state.get('user'):
                raise web.HTTPForbidden(text='Sign in to use line conference')
            if topic:
                if state.get('core_topic') != 1:
                    raise BridgeError('This BBS backend does not support Web TOPIC')
                if action in ('history', 'unread'):
                    return response(session, await session.bridge.view('topic_'+action, 0, cursor))
                if action not in ('post', 'seen', 'delete'):
                    raise ValueError('Invalid topic action')
            if state.get('core_conference') != 1:
                raise BridgeError('This BBS backend does not support the Web line conference')
            if not topic and action in ('rooms', 'history', 'unread', 'position'):
                return response(session, await session.bridge.view('conf_'+action, room, cursor))
            if not topic and action not in ('post', 'seen', 'create'):
                raise ValueError('Invalid conference action')
            body = payload.get('body', '')
            maximum = 80 if action == 'create' else 512
            if (not isinstance(body, str) or len(body.encode()) > maximum
                    or any(ord(c) < 32 or 127 <= ord(c) <= 159 for c in body)):
                raise ValueError('Invalid conference text')
            return response(session, await session.bridge.update(('topic.' if topic else 'conference.')+action,
                                 id=room, arg=cursor, body=body))

    async def chat_send(request):
        session=current(request,mutate=True);payload=await object_body(request)
        body=payload.get('body')
        if not isinstance(body,str) or not body or len(body.encode())>512 or any(ord(c)<32 or ord(c)==127 for c in body):
            raise ValueError('Invalid chat text')
        async with session.bridge.lock:
            if not session.bridge.state.get('chat'):raise BridgeError('Join chat or private talk first')
            if session.bridge.state.get('core_updates') == 1:
                return response(session,await session.bridge.update('chat_send',body=body))
            return response(session,await session.bridge.exchange('/SAY '+body))

    async def compose(request):
        session=current(request,mutate=True);payload=await object_body(request)
        text=payload.get('body','');title=payload.get('title','');kind=payload.get('kind','article')
        if not isinstance(text,str) or not isinstance(title,str):raise ValueError('Invalid text')
        text=text.replace('\r\n','\n')
        lines=text.split('\n')
        if text.endswith('\n'):lines.pop()
        allow_empty=kind=='editing' and session.bridge.state.get('editor_kind') in (40,41,89)
        if not text and allow_empty:lines=[]
        if (not text and not allow_empty) or any(len(line.encode())>4096 or any(ord(c)<32 or ord(c)==127 for c in line) for line in lines):raise BridgeError('Body lines must be text, at most 4096 bytes per line')
        if sum(len(line.encode())+2 for line in lines)>262144:raise BridgeError('Body exceeds 256 KiB')
        if not title or len(title.encode())>80 or any(ord(c)<32 or ord(c)==127 for c in title):raise BridgeError('Title must be 1–80 bytes')
        attachments=payload.get('attachments',[])
        if not isinstance(attachments,list) or len(attachments)>16 or any(type(x) is not int or not 1<=x<=2147483646 for x in attachments):raise ValueError('Invalid attachments')
        async with session.bridge.lock:
            if session.bridge.state.get('secret'):raise BridgeError('Finish or cancel the current password prompt first')
            if session.bridge.state.get('core_updates') == 1:
                if kind not in ('article','mail','reply_mail','editing'):raise ValueError('Invalid compose kind')
                if kind=='editing' and not session.bridge.state.get('editing'):raise BridgeError('No active editor')
                recipient=payload.get('recipient','') if kind=='mail' else ''
                if kind=='mail' and (not isinstance(recipient,str) or not re.fullmatch(r'[a-z0-9_-]{1,32}',recipient)):
                    raise ValueError('Invalid recipient')
                board=int(payload.get('board') or 0) if kind=='article' else 0
                parent=int(payload.get('parent') or 0) if kind in ('article','reply_mail') else 0
                if not 0<=board<=1024 or not 0<=parent<=2147483646 or (kind=='reply_mail' and not parent):
                    raise ValueError('Invalid number')
                result=await session.bridge.update(kind,board=board,id=parent,flag=payload.get('draft') is True,
                    recipient=recipient,title=title,body=''.join(line+'\r\n' for line in lines),
                    attachments=tuple(dict.fromkeys(attachments)))
                return response(session,result)
            if kind=='editing':
                if not session.bridge.state.get('editing'):raise BridgeError('No active editor')
                # Replace using core editing operations; do not discard a saved draft on error.
                if session.bridge.state.get('editor_kind') not in (40,41,89):await session.bridge.exchange('/TITLE '+title)
                result=await session.bridge.exchange('/N')
                if 'ERR ' in result['text']:return response(session,result)
            else:
                if any(session.bridge.state.get(k) for k in ('editing','secret','chat','gather')):raise BridgeError('Finish the current dialogue first')
                if session.bridge.state.get('mail') and kind=='article':await session.bridge.exchange('MAIL EXIT')
                if kind=='mail':
                    recipient=payload.get('recipient','')
                    if not re.fullmatch(r'[a-z0-9_-]{1,32}',recipient):raise ValueError('Invalid recipient')
                    command='MAIL SEND '+recipient+' '+title
                elif kind=='reply_mail':command='MAIL REPLY '+str(int(payload['parent']))+' '+title
                elif kind=='article':
                    if payload.get('board'):
                        selected=await session.bridge.exchange('BOARD '+str(int(payload['board'])))
                        if 'ERR ' in selected['text'] or session.bridge.state.get('secret'):return response(session,selected)
                    command=('REPLY '+str(int(payload['parent']))+' ' if payload.get('parent') else 'POST ')+title
                else:raise ValueError('Invalid compose kind')
                result=await session.bridge.exchange(command)
                if not session.bridge.state.get('editing'):return response(session,result)
            for line in lines:
                result=await session.bridge.literal(line)
                if 'ERR ' in result['text']:return response(session,result)
            if session.bridge.state.get('editor_kind')==109:
                result=await session.bridge.exchange('/DETACH')
                if 'ERR ' in result['text'] or 'MAIL ERROR' in result['text']:return response(session,result)
            for ident in attachments:
                result=await session.bridge.exchange('/ATTACH '+str(ident))
                if 'ERR ' in result['text'] or 'MAIL ERROR' in result['text']:return response(session,result)
            result=await session.bridge.exchange('/SAVE' if payload.get('draft') else '.')
            if payload.get('draft') and result['text'].startswith('DRAFT STORED ') and session.bridge.state.get('editing'):
                finished=await session.bridge.exchange('/CANCEL')
                result['state']=finished['state']
        return response(session,result)

    async def messages(request):
        session=sessions.get(request.cookies.get(cookie,''));handle=session.bridge.state.get('username','') if session else ''
        return web.json_response(presentation.read(request.query.get('lang','ja'),handle))

    async def gather(request):
        session=current(request,mutate=True);payload=await object_body(request);text=payload.get('body')
        lines=None
        if text is not None:
            if not isinstance(text,str):raise ValueError('Invalid Gather text')
            lines=text.replace('\r\n','\n').split('\n')
            if not 1<=len(lines)<=10 or any(len(line)>70 or line in ('//','/CANCEL') or any(ord(c)<32 or ord(c)==127 for c in line) for line in lines):raise ValueError('Gather allows 10 lines of 70 characters; // and /CANCEL are reserved')
        async with session.bridge.lock:
            if any(session.bridge.state.get(k) for k in ('editing','secret','chat','gather','input')):raise BridgeError('Finish the current dialogue first')
            if lines is None and session.bridge.state.get('core_operations')==1:
                cursor=payload.get('cursor',0)
                if type(cursor) is not int or not 0<=cursor<=2147483646:
                    raise ValueError('Invalid Gather position')
                return response(session,await session.bridge.view('gather',0,cursor))
            if lines is not None and session.bridge.state.get('core_updates') == 1:
                return response(session,await session.bridge.update('gather',body=''.join(line+'\r\n' for line in lines)))
            if session.bridge.state.get('mail'):await session.bridge.exchange('MAIL EXIT')
            result=await session.bridge.exchange('GATHER' if lines else 'GATHER READ')
            if not lines:return response(session,result)
            if not session.bridge.state.get('gather'):return response(session,result)
            match=re.search(r'GATHER END NEXT (\d+)',result['text'])
            if match and int(match[1]):await session.bridge.exchange('Q')
            for line in lines:
                result=await session.bridge.exchange(line)
                if result['text'].startswith('ERR '):return response(session,result)
            if session.bridge.state.get('gather'):result=await session.bridge.exchange('//')
        return response(session,result)

    async def events(request):
        session=current(request,terminal_ok=True)
        protocols={part.strip() for part in request.headers.get('Sec-WebSocket-Protocol','').split(',')}
        if 'csrf.'+session.csrf not in protocols:raise web.HTTPForbidden()
        ident=request.cookies.get(cookie,'')
        ws=web.WebSocketResponse(protocols=('liner-notes-v1',),heartbeat=30,max_msg_size=4096)
        await ws.prepare(request)
        try:
            while not ws.closed and not session.bridge.closed:
                if sessions.get(ident) is not session:break
                if session.member and session.bridge.state.get('user') != session.member:break
                text='' if session.guest_lock.locked() else session.bridge.idle_output()
                if text:await ws.send_json({'text':text})
                try:
                    msg=await asyncio.wait_for(ws.receive(),.3)
                    if msg.type in (WSMsgType.CLOSE,WSMsgType.CLOSED,WSMsgType.ERROR):break
                except asyncio.TimeoutError:pass
            if not ws.closed and sessions.get(ident) is session:await ws.send_json({'closed':True})
        finally:await ws.close()
        return ws

    async def terminal(request):
        session = current(request,terminal_ok=True)
        ident = request.cookies.get(cookie,'')
        return await relay(request, session, upstream, lambda: sessions.get(ident) is session)

    async def progress(request):
        session=current(request)
        return web.json_response({'transfers':[{k:v for k,v in item.items() if k!='owner'} for item in transfers.values() if item['owner'] is session]})

    async def upload(request):
        session=current(request,mutate=True)
        if not session.bridge.state.get('user'):raise web.HTTPForbidden()
        if sum(not t['done'] and t['owner'] is session for t in transfers.values())>=2:raise BridgeError('Two uploads are already active')
        asynchronous=request.headers.get('X-Upload-Async')=='1'
        private=request.query.get('kind')=='mail';name=request.headers.get('X-File-Name','')
        # Names are UTF-8 base64 so non-ASCII names survive HTTP headers.
        name=base64.b64decode(name,validate=True).decode('utf-8')
        description=request.headers.get('X-File-Description','')
        description=base64.b64decode(description,validate=True).decode('utf-8') if description else ''
        category=request.headers.get('X-File-Board','')
        if request.headers.get('X-File-Board-Encoded'):category=base64.b64decode(request.headers['X-File-Board-Encoded'],validate=True).decode('utf-8')
        if category=='-':category=''
        resume=int(request.headers.get('X-Upload-Resume','0'))
        if not 0<=resume<=2147483646:raise ValueError('Invalid resume ID')
        if not name or len(name.encode())>100 or any(c in name for c in '/\\\r\n\0') or name in ('.','..'):raise ValueError('Invalid filename')
        if len(description.encode())>240 or len(category.encode())>48 or any(ord(c)<32 for c in description+category):raise ValueError('Invalid description or board')
        total=request.content_length
        if total is None or not 0<=total<=upload_limit:raise web.HTTPRequestEntityTooLarge(max_size=upload_limit,actual_size=total or 0)
        used=sum(x.stat().st_size for x in spool.iterdir() if x.is_file())
        reserved=sum(t['expected']-t['received'] for t in transfers.values() if not t['done'])
        if used+reserved+total>spool_limit:raise BridgeError('Web transfer spool capacity reached')
        token=secrets.token_urlsafe(24);status={'owner':session,'name':name,'kind':'mail' if private else 'public','expected':total,'received':0,'sent':0,'done':False,'id':0,'phase':'receiving'}
        digest=hashlib.sha256()
        fd,temp=tempfile.mkstemp(prefix='upload-',dir=spool)
        transfers[token]=status
        try:
            with os.fdopen(fd,'wb') as stream:
                async for chunk in request.content.iter_chunked(65536):
                    status['received']+=len(chunk)
                    if status['received']>total:raise ValueError('Upload exceeds declared size')
                    stream.write(chunk);digest.update(chunk)
            if status['received']!=total:raise ValueError('Incomplete HTTP upload')
        except BaseException:
            Path(temp).unlink(missing_ok=True);transfers.pop(token,None);raise
        async def register():
            try:
                b64=lambda value:base64.b64encode(value.encode()).decode() or '-'
                prefix='MFILE' if private else 'FILE'
                status['phase']='waiting'
                async with session.bridge.lock:
                    mail_editor=private and session.bridge.state.get('editor_kind')==109 and session.bridge.state.get('transfer_block')
                    if (session.bridge.state.get('editing') and not mail_editor) or any(session.bridge.state.get(k) for k in ('secret','chat','gather')):raise BridgeError('Finish the current dialogue before transferring')
                    if session.bridge.state.get('mail') and not session.bridge.state.get('editing'):await session.bridge.exchange('MAIL EXIT')
                    start=f'{prefix}START {total} {digest.hexdigest()} {b64(name)} {b64(description)}'
                    if category and not private:start=f'FILESTARTIN {total} {digest.hexdigest()} {b64(name)} {b64(description)} {b64(category)}'
                    if resume:
                        ident=resume
                        if session.bridge.state.get('core_session_controls')==1:
                            result=await session.bridge.view('mail_upload' if private else 'upload_status',ident,0)
                            upload_state=result['data'].get('upload')
                        else:
                            result=await session.bridge.exchange(f'{prefix}STATUS {ident}')
                            match=re.search(r'^UPLOADSTATUS '+str(ident)+r' (ACTIVE|COMMITTED|CANCELLED) (\d+) (\d+) ([a-f0-9]{64})\r?$',result['text'],re.M)
                            upload_state=dict(id=ident,state=match[1],received=int(match[2]),size=int(match[3]),sha256=match[4]) if match else None
                        if not upload_state:raise BridgeError(result['text'].strip())
                        if (upload_state['id']!=ident or upload_state['state'] not in ('ACTIVE','COMMITTED')
                                or upload_state['size']!=total or upload_state['sha256']!=digest.hexdigest()
                                or not 0<=upload_state['received']<=total):
                            raise BridgeError('Resume file differs or upload was cancelled')
                        if upload_state['state']=='COMMITTED':
                            resumes.remove(session.bridge.state,status['kind'],ident)
                            return web.json_response({'id':ident,'size':total,'sha256':digest.hexdigest(),'already_committed':True})
                        status['sent']=upload_state['received']
                    else:
                        result=await session.bridge.exchange(start);match=re.search(r'^UPLOAD (\d+)\r?$',result['text'],re.M)
                        if not match:raise BridgeError(result['text'].strip())
                        ident=int(match[1])
                    status['id']=ident;status['phase']='sending'
                    record={'id':ident,'kind':status['kind'],'name':name,'size':total,'sha256':digest.hexdigest(),'category':category,'description':description}
                    resumes.save(session.bridge.state,record)
                    block=bool(session.bridge.state.get('transfer_block'))
                    with open(temp,'rb') as source:
                        source.seek(status['sent'])
                        while chunk:=source.read(65536 if block else 672):
                            if not asynchronous and (request.transport is None or request.transport.is_closing()):raise BridgeError(f'Upload interrupted; resume ID {ident}')
                            result=await session.bridge.exchange(f'{prefix}PUT{"BLOCK" if block else ""} {ident} {status["sent"]} '+base64.b64encode(chunk).decode())
                            expected=f'RECEIVED {ident} {status["sent"]+len(chunk)}'
                            if expected not in result['text'].splitlines():raise BridgeError(result['text'].strip())
                            status['sent']+=len(chunk);session.bridge.last_browser=time.monotonic()
                    status['phase']='verifying'
                    result=await session.bridge.exchange(f'{prefix}FINISH {ident}',timeout=transfer_timeout)
                    receipt=f'UPLOADED {ident} {total} {digest.hexdigest()}'
                    if receipt not in result['text'].splitlines():raise BridgeError('Upload commit was not confirmed: '+result['text'].strip())
                status['done']=True
                resumes.remove(session.bridge.state,status['kind'],ident)
                return web.json_response({'id':ident,'size':total,'sha256':digest.hexdigest(),'transfer':token})
            except (BridgeError,OSError,asyncio.TimeoutError) as error:
                return web.json_response({'error':str(error) or 'Transfer interrupted','resume_id':status['id'],
                    'name':name,'kind':status['kind'],'sent':status['sent'],'expected':total,'phase':status['phase']},status=409)
            finally:
                status['done']=True
                Path(temp).unlink(missing_ok=True)
                status['completed']=time.monotonic()

        if not asynchronous:return await register()
        async def background():
            try:
                result=await register();status['result']=json.loads(result.body);status['http_status']=result.status
            except asyncio.CancelledError:
                status['result']={'error':'Registration interrupted; check resume records','resume_id':status['id']};status['http_status']=409
                raise
            except Exception:
                status['result']={'error':'Registration failed; check resume records','resume_id':status['id']};status['http_status']=503
        task=asyncio.create_task(background());transfer_tasks.add(task);task.add_done_callback(transfer_tasks.discard)
        return web.json_response({'transfer':token,'accepted':True},status=202)

    async def transfer_status(request):
        session=current(request);item=transfers.get(request.match_info['token'])
        if not item or item['owner'] is not session:raise web.HTTPNotFound()
        return web.json_response({k:v for k,v in item.items() if k!='owner'})

    async def resume_list(request):
        session=current(request)
        rows=resumes.list(session.bridge.state)
        for row in rows:row['active']=any(not t['done'] and t['id']==row['id'] and t['kind']==row['kind'] and t['owner'].bridge.state.get('user')==session.bridge.state.get('user') for t in transfers.values())
        return web.json_response({'uploads':rows})

    async def download_ticket(request):
        session=current(request,mutate=True);body=await object_body(request)
        ident=int(body['id'])
        if not 1<=ident<=2147483646:raise ValueError('Invalid file ID')
        name=body.get('name',f'file-{ident}.bin')
        if not isinstance(name,str) or not name or len(name.encode())>100 or any(c in name for c in '/\\\r\n\0'):raise ValueError('Invalid filename')
        now=time.monotonic();session.downloads={key:item for key,item in session.downloads.items() if item['expiry']>now}
        if len(session.downloads)>=16:raise BridgeError('Download ticket capacity reached')
        token=secrets.token_urlsafe(32);session.downloads[token]={'id':ident,'kind':body.get('kind'),'name':name,'expiry':now+60}
        return web.json_response({'url':base+'api/download/'+token})

    async def download(request):
        session=current(request,mutate=request.method=='POST')
        if request.method=='GET':
            body=session.downloads.pop(request.match_info['ticket'],None)
            if not body or body['expiry']<time.monotonic():raise web.HTTPNotFound()
        else:body=await object_body(request)
        ident=int(body['id']);private=body.get('kind')=='mail'
        if not session.bridge.state.get('user') or not 1<=ident<=2147483646:raise web.HTTPForbidden()
        prefix='MFILE' if private else 'FILE';ticket=None
        async with session.bridge.lock:
            if any(session.bridge.state.get(k) for k in ('editing','secret','chat','gather')):raise BridgeError('Finish the current dialogue before transferring')
            if session.bridge.state.get('mail'):await session.bridge.exchange('MAIL EXIT')
            result=await session.bridge.exchange(f'{prefix}META {ident}' if private else f'FILEOPEN {ident}')
            match=re.search(r'^FILE '+str(ident)+r' (\d+) ([a-f0-9]{64})\r?$',result['text'],re.M) if private else re.search(r'^DOWNLOAD ([a-f0-9]{32}) (\d+) ([a-f0-9]{64})\r?$',result['text'],re.M)
            if not match:raise BridgeError(result['text'].strip())
            if private:size,digest=int(match[1]),match[2]
            else:ticket,size,digest=match[1],int(match[2]),match[3]
            response_stream=web.StreamResponse(headers={'Content-Type':'application/octet-stream','Content-Disposition':f'attachment; filename="file-{ident}.bin"','X-File-Size':str(size),'Content-Length':str(size),'Cache-Control':'no-store','X-Content-Type-Options':'nosniff','X-Content-SHA256':digest})
            if request.method=='GET':response_stream.headers['Content-Disposition']="attachment; filename*=UTF-8''"+quote(body['name'],safe='')
            await response_stream.prepare(request)
            offset=0;computed=hashlib.sha256();last=b'';block=bool(session.bridge.state.get('transfer_block'))
            try:
                while offset<size:
                    result=await session.bridge.exchange(f'MFILEGET{"BLOCK" if block else ""} {ident} {offset}' if private else f'FILEFETCH{"BLOCK" if block else ""} {ticket} {offset}')
                    match=re.search(r'^FILEDATA '+str(ident)+' '+str(offset)+r' (\d+) ([A-Za-z0-9+/=]+|-)\r?$',result['text'],re.M) if private else re.search(r'^DOWNLOADDATA '+str(offset)+r' (\d+) ([A-Za-z0-9+/=]+|-)\r?$',result['text'],re.M)
                    if not match:raise BridgeError('Download response invalid')
                    chunk=base64.b64decode(match[2],validate=True) if match[2]!='-' else b''
                    if not chunk or len(chunk)!=int(match[1]) or offset+len(chunk)>size:raise BridgeError('Download length invalid')
                    computed.update(chunk);offset+=len(chunk);session.bridge.last_browser=time.monotonic()
                    if offset==size:last=chunk
                    else:await response_stream.write(chunk)
                if computed.hexdigest()!=digest:raise BridgeError('Download digest mismatch')
                if ticket:
                    receipt=await session.bridge.exchange(f'FILEDONE {ticket} {digest}')
                    if f'DOWNLOADED {ident}' not in receipt['text'].splitlines():raise BridgeError('Download completion not confirmed')
                if last:await response_stream.write(last)
                await response_stream.write_eof()
            except BaseException:
                if ticket and not session.bridge.closed:
                    try:await session.bridge.exchange('FILEABORT '+ticket)
                    except (OSError,BridgeError):pass
                if request.transport:request.transport.close()
                raise
            return response_stream

    async def index(request):
        directory=Path(cfg.get('templates',str(ROOT/'templates')))
        def template(name):
            path=directory/name
            try:
                try:return path.read_text(encoding='utf-8')
                except FileNotFoundError:return (ROOT/'templates'/name).read_text(encoding='utf-8')
            except (OSError,UnicodeError) as error:raise TemplateError(name) from error
        return web.Response(text=template('index.html').replace('{{components}}',template('components.html')),content_type='text/html')
    async def asset(request):
        name=request.match_info['name']
        if name not in ('app.js','reports-ui.js','style.css','xterm.js','xterm-fit.js','xterm.css','terminal-ui.js'):raise web.HTTPNotFound()
        return web.FileResponse(ROOT/'static'/name)

    async def lifetime(app):
        async def sweep():
            while True:
                await asyncio.sleep(15)
                for token,item in list(transfers.items()):
                    if item['done'] and time.monotonic()-item.get('completed',0)>1800:transfers.pop(token,None)
                for ident,session in list(sessions.items()):
                    if session.bridge.closed or time.monotonic()-session.bridge.last_browser>cfg.get('browser_idle_seconds',1800):
                        if session.terminal_ws:await session.terminal_ws.close()
                        sessions.pop(ident,None);await session.bridge.close()
        task=asyncio.create_task(sweep())
        yield
        task.cancel()
        await asyncio.gather(task,return_exceptions=True)
        for transfer in list(transfer_tasks):transfer.cancel()
        await asyncio.gather(*transfer_tasks,return_exceptions=True)
        await asyncio.gather(*(s.terminal_ws.close() for s in sessions.values() if s.terminal_ws),return_exceptions=True)
        await asyncio.gather(*(s.bridge.close() for s in sessions.values()),return_exceptions=True)
    app.cleanup_ctx.append(lifetime)
    app.router.add_get(base,index)
    app.router.add_get(base+'assets/{name}',asset)
    for path,handler in [('session',new_session),('logout',logout),('auth',authenticate),('action',operate),('query',query),('input',input_line),('secret',secret_input),('compose',compose),('gather',gather),('conference',conference),('chat',chat_send),('upload',upload),('download',download)]:app.router.add_post(base+'api/'+path,handler)
    app.router.add_get(base+'api/session',session_info)
    app.router.add_get(base+'api/link',link_control)
    app.router.add_post(base+'api/link',link_control)
    app.router.add_get(base+'api/messages',messages)
    app.router.add_get(base+'api/events',events)
    app.router.add_get(base+'api/terminal',terminal)
    app.router.add_get(base+'api/progress',progress)
    app.router.add_get(base+'api/resumes',resume_list)
    app.router.add_get(base+'api/transfer/{token}',transfer_status)
    app.router.add_post(base+'api/download-ticket',download_ticket)
    app.router.add_get(base+'api/download/{ticket}',download)
    return app
