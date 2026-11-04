"""Presentation-only decoding of fixed raw report headers; never grants rights."""
import re

# Only recognize rows under their protocol's header. Article bodies and user
# supplied guide text must never be interpreted as management records.
SPECS={
 'CONNECTIONS':('CONNECTION',('connection','login','state','transport','peer','seconds','idle','received','sent','queued','columns','rows')),
 'USERLOG':('USERROW',('id','connection','user','event','timestamp','seconds')),
 'FILEHISTORY':('DOWNLOADROW',('id','file','user','timestamp','bytes','protocol')),
 'FILESTATS':('DOWNLOADCOUNT',('file','downloads')),
 'BOARDACCESS':('ACCESSROW',('id','article','user','event','timestamp')),
 'SIGOPS':('SIGOP',('board','user')),
 'BATCH':('BATCH FILE',('file','status')),
 'USERTABLE':('SESSIONROW',('connection','user','login_at','logout_at','seconds','logins','transport')),
 'BOARDLOG':('BOARDCHANGE',('id','user','timestamp','description')),
 'MEMBERS':('MEMBER',('user','login','status')),
 'AUDIT':('EVENT',('id','date','user','login','event','target','board')),
}

def parse_report(text):
    lines=text.split('\r\n');header=lines[0].split(' ',1)[0]
    if header=='RIGHTS':
        m=re.fullmatch(r'RIGHTS (\d+) LEVEL (\d+) GROUPS (\S+) STATUS (\S+)',lines[0])
        if m:return {'kind':'fields','title':header,'fields':list(zip(('user','level','groups','status'),m.groups()))}
    if header=='PREFS':
        m=re.fullmatch(r'PREFS (STORED|SAVED|LOADED) (ja|en) (ON|OFF)',lines[0])
        if m:return {'kind':'fields','title':header,'fields':list(zip(('status','language','pager'),m.groups()))}
    if header in SPECS:
        prefix,columns=SPECS[header];rows=[];extra=[];next_id=0
        for line in lines[1:]:
            if line.startswith(prefix+' '):
                values=line[len(prefix)+1:].split(' ',len(columns)-1)
                if len(values)!=len(columns):return None
                rows.append(values)
            elif re.fullmatch(r'END NEXT \d+',line):next_id=int(line.rsplit(' ',1)[1])
            elif line and not line.startswith('END '):extra.append(line)
        return {'kind':'table','title':header,'columns':columns,'rows':rows,'next':next_id,'details':extra}
    if header=='USERSTATS':
        m=re.fullmatch(r'USERSTATS (\d+) LOGINS (\d+) SECONDS (\d+) LAST (\d+)',lines[0])
        if m:return {'kind':'table','title':header,'columns':('user','logins','seconds','timestamp'),'rows':[m.groups()],'next':0,'details':[]}
    if header=='FILEAUDIT':
        rows=[];cursor=0
        for line in lines[1:]:
            m=re.fullmatch(r'VEVENT (\d+) (\d+) (\S+) (\S)\t([^\t]*)\t([^\t]*)\t([^\t]*)',line)
            if m:rows.append(m.groups())
            elif re.fullmatch(r'END NEXT \d+',line):cursor=int(line.rsplit(' ',1)[1])
            elif line:return None
        return {'kind':'table','title':header,'columns':('id','file','date','event','user','before','after'),'rows':rows,'next':cursor,'details':[]}
    # These reports contain named fields. Preserve values, including spaces.
    if header in ('RIGHTS','PREFS','BOARDSETTING','FILEBOARDSETTING','APPLICATION','UPLOADSTATUS','FILE','FILEMETA'):
        if any(line.startswith('ERR ') for line in lines):return None
        fields=[]
        for line in lines:
            if not line or line.startswith('END '):continue
            name,_,value=line.partition(' ');fields.append([name,value])
        return {'kind':'fields','title':header,'fields':fields}
    return None
