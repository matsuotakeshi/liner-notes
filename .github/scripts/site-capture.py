from pathlib import Path
import sys,tempfile,json,re,hashlib
sys.path.insert(0,str(Path('tests').resolve()))
from test_board import Server
from test_runtime import Client as Raw
from test_telnet import Client as Telnet
from test_members import auth
import pyte
from PIL import Image,ImageDraw,ImageFont
root=Path.cwd();out=root/'capture-output';out.mkdir(exist_ok=True)
profiles=('rtbbs','mmm','mash','ktbbs','big-model','wani-bbs','net-cock','vs')
prompts=(b'] > ',b'MAIN>> ',b'MAIN>> ',b'MAIN> ',b'BIG> ',b'/> ',b'Menu> ',b'VS> ')
font=ImageFont.truetype('/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc',18)
mono=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf',18)
def write_screen(lang,profile,data):
 text=data.decode('utf-8','replace')
 if '\ufffd' in text:raise ValueError('invalid UTF-8 '+lang+' '+profile)
 screen=pyte.Screen(80,24);stream=pyte.Stream(screen);stream.feed(text)
 assert any(x.strip() for x in screen.display)
 image=Image.new('RGB',(980,608),'#101d23');draw=ImageDraw.Draw(image)
 draw.text((16,10),f'Liner Notes v0.76 / {profile} / {lang} / 80x24',font=mono,fill='#93b7a3')
 draw.line((16,39,964,39),fill='#39564e',width=1)
 for y,line in enumerate(screen.display):
  for x,ch in enumerate(line):
   if not ch or ch==' ' or ch=='\x00':continue
   f=font if ord(ch)>127 else mono
   draw.text((16+x*11.8,48+y*22.5),ch,font=f,fill='#c2e7c1',stroke_width=0)
 name=profile+'-'+lang
 image.save(out/(name+'.png'))
 (out/(name+'.txt')).write_text('\n'.join(screen.display)+'\n',encoding='utf-8')
 return hashlib.sha256(data).hexdigest()
manifest=[]
for lang in ('en','ja'):
 with tempfile.TemporaryDirectory(prefix='liner-site-capture-') as tmp:
  p=Path(tmp);b=p/'notes.dat';m=p/'members.dat'
  server=Server(str(root/'runtime'),b,members=m)
  try:
   c=Raw(server.port)
   try:auth(c,'visitor',True)
   finally:c.close()
  finally:server.close()
  options=['--ui-policy','MEMBERS','--ui-allow',','.join(profiles)]
  server=Server(str(root/'runtime'),b,members=m,language=lang,telnet=True,menu=True,options=options)
  try:
   c=Telnet(server.port)
   try:
    c.until(prompts[0]);c.cmd('LOGIN visitor',b'> ');c.cmd('test-only-secret-8721',prompts[0])
    first=c.cmd('MENU',prompts[0]);manifest.append({'ui':profiles[0],'lang':lang,'raw_sha256':write_screen(lang,profiles[0],first)})
    for i in range(1,len(profiles)):
     data=c.cmd('UI '+profiles[i],prompts[i]);manifest.append({'ui':profiles[i],'lang':lang,'raw_sha256':write_screen(lang,profiles[i],data)})
   finally:c.close()
  finally:server.close()
(out/'manifest.json').write_text(json.dumps({'source_commit':'66b157e7b76a9e0be23fd870de63adc5afbddc58','method':'fresh isolated runtime, Telnet login, UI switch; pyte 80x24 screen render','images':manifest},indent=2)+'\n',encoding='utf-8')
print('Captured',len(manifest),'screens; no live service or account used.')
