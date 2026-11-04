"""Web presentation of the shared unprivileged BBS transport."""
import re
from .bbs_transport import Bridge,BridgeError,TelnetDecoder
from .reports import parse_report

def response_data(text):
    """Interpret documented raw records. Never turn user content into HTML."""
    lines = text.splitlines()
    data = {'boards': [], 'notes': [], 'mail': [], 'profiles': [], 'files': [], 'file_boards': [], 'tree': [], 'matches': [], 'next': 0}
    for line in lines:
        m=re.fullmatch(r'VBOARD ([01]) (\S+)(?: \| (.*))?',line)
        if m:data['file_boards'].append({'upload':m[1]=='1','path':m[2],'title':m[3] or m[2]})
        m = re.fullmatch(r'FILEITEM (\d+) (\d+) (.*)', line)
        if m: data['files'].append({'id':int(m[1]),'size':int(m[2]),'name':m[3]})
        m = re.fullmatch(r'FILEDETAIL (\d+) (ASCII|BINARY) (\d+) (.*)', line)
        if m:
            for item in data['files']:
                if item['id']==int(m[1]):item.update(attribute=m[2],downloads=int(m[3]),description=m[4])
        m = re.fullmatch(r'\s*#(\d+) parent=(\d+) depth=(\d+) write=(yes|no) unread=(\d+|-) (.*)', line)
        if m: data['boards'].append(dict(zip(('id','parent','depth','write','unread','title'),m.groups())))
        m = re.fullmatch(r'#(\d+) replies=(\d+) latest=(\d+) updated=(\S+) unread=(\d+|-) (.*)', line)
        if m: data['notes'].append(dict(zip(('id','replies','latest','updated','unread','title'),m.groups())))
        m = re.fullmatch(r'MAIL ROW (\d+) (\S+) (\S+) ([01]) (.*)', line)
        if m: data['mail'].append(dict(zip(('id','sender','recipient','read','title'),m.groups())))
        m = re.fullmatch(r'(?:MAIL )?END NEXT (\d+)', line)
        if m: data['next'] = int(m[1])
        m=re.fullmatch(r'\s*#(\d+) parent=(\d+) depth=(\d+) (.*)',line)
        if m and text.startswith('TREE '):data['tree'].append(dict(zip(('id','parent','depth','title'),m.groups())))
        m=re.fullmatch(r'(\d+) (\d+) (\d+) (.*)',line)
        if m and text.startswith(('FIND ','NEWS ')):data['matches'].append(dict(zip(('id','board','parent','title'),m.groups())))
    m = re.search(r'(?:^|\r\n)ARTICLE (\d+) PARENT (\d+)([^\r\n]*)\r\nTITLE ([^\r\n]*)\r\nBODY (\d+)\r\n',text)
    if m:
        body = text[m.end():].encode('utf-8')[:int(m[5])].decode('utf-8')
        data['article'] = {'id':int(m[1]),'parent':int(m[2]),'meta':m[3].strip(),'title':m[4],'body':body}
    m = re.search(r'(?:^|\r\n)MAIL ITEM (\d+) FROM (\S+) TO (\S+) READ (\d+) REPLY (\d+)\r\nTITLE ([^\r\n]*)\r\nBODY\r\n',text)
    if m:
        end = text.rfind('MAIL END\r\n')
        data['message'] = {'id':int(m[1]),'sender':m[2],'recipient':m[3],'read':int(m[4]),'parent':int(m[5]),'title':m[6],'body':text[m.end():end]}
    if text.startswith('PROFILES '):
        for line in lines[1:]:
            m=re.fullmatch(r'(\d+) (\S+) (.*)',line)
            if m:data['profiles'].append({'id':int(m[1]),'login':m[2],'handle':m[3]})
    m=re.match(r'PROFILE (\d+) (\S+)\r\n',text)
    if m:
        profile={'id':int(m[1]),'login':m[2],'fields':{}}
        for line in lines[1:]:
            if line.startswith(('PROFILEBODY ','END PROFILE')):break
            key,_,value=line.partition(' ')
            if key:profile['fields'][key]=value
        bio=re.search(r'\r\nPROFILEBODY (\d+)\r\n',text)
        if bio:profile['fields']['BIO']=text[bio.end():].encode()[:int(bio[1])].decode()
        data['profile']=profile
    report=parse_report(text)
    if report:data['report']=report
    return data
