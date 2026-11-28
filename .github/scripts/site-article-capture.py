from pathlib import Path
import sys,tempfile,json,hashlib,unicodedata,traceback
sys.path.insert(0,str(Path('tests').resolve()))
from test_board import Server,post
from test_runtime import Client as Raw
from test_telnet import Client
from test_members import auth
import pyte
from PIL import Image,ImageDraw,ImageFont
out=Path('capture-output');out.mkdir(exist_ok=True);root=Path.cwd();profiles=['rtbbs','mmm','mash','ktbbs','big-model','wani-bbs','net-cock','vs']
prompts=[b'] > ',b'MAIN>> ',b'MAIN>> ',b'MAIN> ',b'BIG> ',b'/> ',b'Menu> ',b'VS> ']
steps={
'rtbbs':[('LIST',b'] > ')],
'mmm':[('2',b'NOTES>> '),('open -i #1',b'INDEX> ')],
'mash':[('2',b'NOTES>> '),('open -i #1',b'INDEX> ')],
'ktbbs':[('R',b'Board (? list / 0 back) > '),('1',b'READ> '),('L',b'Start article # (Enter: newest) > '),('',b'LIST> ')],
'big-model':[('B',b'SIG> '),('1',b'BOARD> '),('R',b'LIST> ')],
'wani-bbs':[('b',b'/board> '),('1',b'/board/1> '),('l',b'/board/1 index> ')],
'net-cock':[('2',b'Menu> '),('1',b'Board> '),('1',b'Read> '),('A',b'Read> ')],
'vs':[('B #1',b'Message> '),('L 1',b'Message> ')]}
mono=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf',18); cjk=ImageFont.truetype('/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc',18)
results=[]
for lang in ('en','ja'):
 for i,profile in enumerate(profiles):
  try:
   with tempfile.TemporaryDirectory(prefix='liner-capture-') as tmp:
    p=Path(tmp); b=p/'notes';m=p/'members';cat=p/'catalog';cat.write_text('1|0|all|member|General\n',encoding='utf8')
    s=Server(str(root/'runtime'),b,members=m,catalog=cat)
    try:
     c=Raw(s.port);auth(c,'visitor',True);post(c,'Welcome to Liner Notes','Hello from an isolated test BBS.');post(c,'Reply to welcome','A sample reply.',1);c.close()
    finally:s.close()
    s=Server(str(root/'runtime'),b,members=m,catalog=cat,language=lang,telnet=True,menu=True,options=['--ui-policy','MEMBERS','--ui-allow',','.join(profiles)])
    try:
     c=Client(s.port);c.sock.settimeout(5);c.until(prompts[0]);c.cmd('LOGIN visitor',b'> ');c.cmd('test-only-secret-8721',prompts[0]);
     if i:c.cmd('UI '+profile,prompts[i])
     chunks=[]
     for cmd,marker in steps[profile]:
      if profile=='ktbbs' and lang=='ja':
       marker={b'Board (? list / 0 back) > ':'掲示板 (? 一覧 / 0 戻る) > '.encode(),b'Start article # (Enter: newest) > ':'開始記事 # (Enter: 最新) > '.encode()}.get(marker,marker)
      print('STEP',profile,lang,repr(cmd),flush=True)
      chunks.append(c.cmd(cmd,marker))
     data=chunks[-1];assert b'Welcome' in data,(profile,data[-500:])
     screen=pyte.Screen(80,24);pyte.Stream(screen).feed(data.decode('utf8','strict'));lines=screen.display
     im=Image.new('RGB',(1000,660),'#101d23');d=ImageDraw.Draw(im);d.text((20,12),f'Liner Notes v0.76 / {profile} / {lang} / articles',font=mono,fill='#93b7a3')
     for y,line in enumerate(lines):
      x=0
      for ch in line:
       width=2 if unicodedata.east_asian_width(ch) in ('W','F') else 1
       if ch!=' ':d.text((20+x*12,51+y*24.5),ch,font=cjk if ord(ch)>127 else mono,fill='#c2e7c1')
       x+=width
     name='ui-'+profile+'-articles-'+lang;im.save(out/(name+'.png'));(out/(name+'.txt')).write_text('\n'.join(lines)+'\n',encoding='utf8');results.append({'ui':profile,'lang':lang,'ok':True,'raw_sha256':hashlib.sha256(data).hexdigest()})
     if profile=='rtbbs':
      data=c.cmd('TREE 1',b'] > ');assert b'Welcome' in data and b'Reply to welcome' in data
      screen=pyte.Screen(80,24);pyte.Stream(screen).feed(data.decode('utf8','strict'));lines=screen.display
      im=Image.new('RGB',(1000,660),'#101d23');d=ImageDraw.Draw(im);d.text((20,12),f'Liner Notes v0.76 / {profile} / {lang} / tree',font=mono,fill='#93b7a3')
      for y,line in enumerate(lines):
       x=0
       for ch in line:
        width=2 if unicodedata.east_asian_width(ch) in ('W','F') else 1
        if ch!=' ':d.text((20+x*12,51+y*24.5),ch,font=cjk if ord(ch)>127 else mono,fill='#c2e7c1')
        x+=width
      name='ui-rtbbs-tree-'+lang;im.save(out/(name+'.png'));(out/(name+'.txt')).write_text('\n'.join(lines)+'\n',encoding='utf8');results.append({'ui':'rtbbs-tree','lang':lang,'ok':True,'raw_sha256':hashlib.sha256(data).hexdigest()})
     c.close()
    finally:s.close()
  except Exception as e:results.append({'ui':profile,'lang':lang,'ok':False,'error':str(e)[:300]});print('FAILED',profile,lang,type(e).__name__,str(e)[:150],flush=True)
(out/'article-capture-manifest.json').write_text(json.dumps({'source_commit':'66b157e7b76a9e0be23fd870de63adc5afbddc58','results':results},indent=2),encoding='utf8')
print('Captured',sum(x['ok'] for x in results),'of',len(results),flush=True)
