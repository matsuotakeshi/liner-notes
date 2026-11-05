#!/usr/bin/env python3
"""Build the allowlisted canonical book, offline edition and legacy redirects.

No root *.md glob is a publication source. Historical records are never rendered.
"""
from pathlib import Path
import argparse,hashlib,html,json,re,shutil,zipfile
from urllib.parse import urlsplit
import markdown
from markdown.extensions.toc import slugify_unicode

ROOT=Path(__file__).resolve().parents[2]
DOC=ROOT/'docs/manual'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def convert(text):
    body=markdown.markdown(text,extensions=['extra','toc','sane_lists'],extension_configs={'toc':{'slugify':slugify_unicode}})
    def fix(m):
        href=html.unescape(m[1]);u=urlsplit(href)
        if not u.scheme and not u.netloc and u.path.endswith('.md'):
            href=u.path[:-3]+'.html'+('#'+u.fragment if u.fragment else '')
        return 'href="'+html.escape(href,quote=True)+'"'
    return re.sub(r'href="([^"]+)"',fix,body)
def plain(s):return html.unescape(re.sub(r'<[^>]+>',' ',s))
def build(out):
    if out.exists():raise SystemExit('Output must be a new directory: '+str(out))
    m=json.loads((DOC/'manifest.json').read_text(encoding='utf8'))
    pages=m['pages'];ids=[p['id'] for p in pages]
    if len(ids)!=len(set(ids)):raise SystemExit('Duplicate publication ID')
    if any(not re.fullmatch('[a-z][a-z0-9-]*',i) or 'stage' in i for i in ids):raise SystemExit('Invalid public page ID')
    expected={i+'.md' for i in ids}
    if {p.name for p in DOC.glob('*.md')}!=expected:raise SystemExit('Unmanifested/missing canonical Markdown')
    out.mkdir(parents=True);book=out/'manual';book.mkdir();(out/'assets').mkdir()
    for p in (DOC/'assets').iterdir():shutil.copyfile(p,out/'assets'/p.name)
    shutil.copyfile(ROOT/'LICENSE',book/'LICENSE')
    def nav(current):
        s='<a class="manual-back" href="index.html">統合目次</a><p class="manual-version">Liner Notes '+html.escape(m['version'])+'<br>文書改訂 '+m['revision']+'</p>'
        for no,title in enumerate(m['chapters'],1):
            subset=[p for p in pages if p['chapter']==no]
            opened=' open' if any(p['id']==current for p in subset) else ''
            s+=f'<details{opened}><summary>第{no}章 {html.escape(title)}</summary><ol>'
            for p in subset:
                attr=' aria-current="page"' if p['id']==current else ''
                s+=f'<li><a{attr} href="{p["id"]}.html">{html.escape(p["title"])}</a></li>'
            s+='</ol></details>'
        return s
    search='<div class="manual-filter"><label for="manual-search">このマニュアルを検索</label><input id="manual-search" type="search" placeholder="例：添付 再開、SIGOP、port 23" autocomplete="off"><p id="manual-search-result" role="status" aria-live="polite"></p><ol id="manual-search-results" hidden></ol></div>'
    def shell(title,body,current='',toc='',lang='ja',depth=''):
        # All book links and search data remain file:// compatible.
        prefix='../' if not depth else '../../'
        bookprefix='' if not depth else '../'
        sidebar=nav(current)
        if depth:sidebar=re.sub('href="(?!https)', 'href="../',sidebar)
        return '<!doctype html>\n<html lang="'+lang+'"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>'+html.escape(title)+' | Liner Notes '+m['version']+' マニュアル</title><meta name="description" content="Liner Notes '+m['version']+'の統合マニュアル。導入・利用・管理・接続・設定・障害対応。"><link rel="canonical" href="https://linernotes.site/manual/'+(current+'.html' if current else depth+'index.html')+'"><link rel="stylesheet" href="'+prefix+'assets/style.css"><link rel="stylesheet" href="'+prefix+'assets/manual.css"></head><body class="manual-body"><header><a class="brand" href="https://linernotes.site/">Liner Notes</a><nav aria-label="サイト"><a href="'+bookprefix+'index.html">マニュアル目次</a><a href="'+bookprefix+'en/index.html">English guide</a><a href="https://github.com/matsuotakeshi/liner-notes/releases/tag/v'+m['version']+'">ソフトウェア '+m['version']+'</a></nav></header><div class="manual-layout"><aside class="manual-sidebar" aria-label="章の目次">'+sidebar+'</aside><main class="manual-article"><div class="manual-breadcrumb"><a href="'+bookprefix+'index.html">統合マニュアル</a> / '+html.escape(title)+'</div>'+(search if not depth else '')+toc+body+'<footer class="manual-footer">対象 Liner Notes '+m['version']+' / 文書改訂 '+m['revision']+'<br><a href="'+bookprefix+'index.html">全体目次</a> · <a href="'+bookprefix+'LICENSE">GPL-3.0-only</a></footer></main></div>'+('' if depth else '<script src="search-data.js"></script><script src="../assets/manual.js"></script>')+'</body></html>\n'
    entries=[];hashes={}
    for n,p in enumerate(pages):
        text=(DOC/(p['id']+'.md')).read_text(encoding='utf8');body=convert(text)
        headings=re.findall(r'<h([23]) id="([^"]+)">(.*?)</h\1>',body,re.S)
        toc='<details class="page-toc"><summary>このページの目次</summary><ol>'+''.join('<li><a href="#'+a+'">'+plain(t)+'</a></li>' for _,a,t in headings)+'</ol></details>' if headings else ''
        more='<nav class="book-sequence" aria-label="前後のページ">'
        if n:more+='<a href="'+pages[n-1]['id']+'.html">← '+html.escape(pages[n-1]['title'])+'</a>'
        if n+1<len(pages):more+='<a href="'+pages[n+1]['id']+'.html">'+html.escape(pages[n+1]['title'])+' →</a>'
        more+='</nav>'
        full=shell(p['title'],'<article id="manual-content">'+body+'</article>'+more,p['id'],toc)
        (book/(p['id']+'.html')).write_text(full,encoding='utf8',newline='\n')
        shutil.copyfile(DOC/(p['id']+'.md'),book/(p['id']+'.md'))
        # Search per section, with real fragment anchors; scripts/data never contain archives.
        chunks=re.split(r'(?=<h[23] id=")',body)
        for chunk in chunks:
            h=re.search(r'<h[23] id="([^"]+)">(.*?)</h[23]>',chunk,re.S)
            entries.append(dict(url=p['id']+'.html'+('#'+h[1] if h else ''),title=p['title']+(' / '+plain(h[2]) if h else ''),text=re.sub(r'\s+',' ',plain(chunk)).strip()))
        hashes[p['id']]=dict(markdown=sha(book/(p['id']+'.md')),html=sha(book/(p['id']+'.html')))
    home='<h1>Liner Notes 統合マニュアル</h1><p>現行安定版 '+m['version']+' の機能・操作・設定を、目的別にまとめた一冊です。文書改訂 '+m['revision']+'。</p><p><a href="install-freebsd.html">FreeBSD導入</a> · <a href="install-linux.html">Linux導入</a> · <a href="accounts.html">利用を始める</a> · <a href="troubleshooting.html">困ったとき</a></p>'
    for no,title in enumerate(m['chapters'],1):
        home+=f'<section class="manual-group"><h2>第{no}章 {html.escape(title)}</h2><ul class="manual-catalog">'
        home+=''.join('<li><a href="'+p['id']+'.html"><strong>'+html.escape(p['title'])+'</strong></a></li>' for p in pages if p['chapter']==no)
        home+='</ul></section>'
    home+='<p><a href="../downloads/liner-notes-'+m['version']+'-manual-'+m['revision'].replace('-','')+'.zip">オフラインHTML・Markdown一式</a>（公開サイトから取得）</p>'
    # Offline ZIP cannot contain itself; replace this one external-only download link.
    home=home.replace('../downloads/','https://linernotes.site/downloads/')
    (book/'index.html').write_text(shell('統合目次',home),encoding='utf8',newline='\n')
    (book/'en').mkdir()
    eng='<h1>Liner Notes manual</h1><p>This is the canonical manual for stable version '+m['version']+', revised '+m['revision']+'. The full manual is currently in Japanese. The application supports Japanese and English; an English translation of the book is not yet provided.</p><p><a href="../index.html">Open the seven-chapter manual</a></p><ol>'+''.join('<li><a href="../'+next(p['id'] for p in pages if p['chapter']==n)+'.html">'+t+'</a></li>' for n,t in enumerate(['Overview','Installation and updates','User guide','Administrator and SIGOP guide','Connections and integrations','Configuration and technical reference','Troubleshooting'],1))+'</ol><p>Historical implementation reports are kept in the development repository and are not included in book navigation or search.</p>'
    (book/'en/index.html').write_text(shell('English guide',eng,lang='en',depth='en/'),encoding='utf8',newline='\n')
    (book/'search-data.js').write_text('window.LINER_MANUAL_SEARCH='+json.dumps(entries,ensure_ascii=False).replace('<','\\u003c')+';\n',encoding='utf8',newline='\n')
    legacy=json.loads((DOC/'legacy-urls.json').read_text(encoding='utf8'));redirects=[]
    anchor_map=legacy.get('anchors',{})
    for old,target in sorted(legacy['documents'].items()):
        stem=Path(old).stem;dest=target+'.html';mapping=anchor_map.get(old,{})
        if old=='INSTALL.md':mapping={**{a:p+'.html#'+a for a,p in legacy['install_anchors'].items()},**mapping}
        content='<!doctype html><html lang="ja"><head><meta charset="utf-8"><meta name="robots" content="noindex,follow"><meta http-equiv="refresh" content="0;url='+dest+'"><link rel="canonical" href="https://linernotes.site/manual/'+dest+'"><title>マニュアルの移転</title></head><body><p>説明は機能別マニュアルへ統合しました。<a href="'+dest+'">'+html.escape(next(p['title'] for p in pages if p['id']==target))+'</a>を開いてください。</p><script>const routes='+json.dumps(mapping,ensure_ascii=False).replace('<','\\u003c')+';let key;try{key=decodeURIComponent(location.hash.slice(1))}catch(e){key=""}location.replace(routes[key]||'+json.dumps(dest)+');</script></body></html>\n'
        (book/(stem+'.html')).write_text(content,encoding='utf8',newline='\n')
        # Static hosts cannot redirect raw Markdown with HTML. Replace obsolete content with a pointer.
        (book/old).write_text('# マニュアルの移転\n\n[現行の説明]('+target+'.md)へ統合しました。\n',encoding='utf8',newline='\n')
        redirects.append({'old':stem+'.html','new':dest,'fragments':mapping})
        redirects.append({'old':old,'new':target+'.md','fragments':{}})
    (book/'redirects.json').write_text(json.dumps(redirects,ensure_ascii=False,indent=2)+'\n',encoding='utf8',newline='\n')
    # Use static fragment-aware redirects for HTML. Apache rules only redirect raw MD.
    rules='# Optional: merge into existing Apache configuration; do not replace server rules.\n'
    rules+='\n'.join('RedirectMatch 301 ^/manual/'+re.escape(Path(old).stem)+r'\.md$ /manual/'+target+'.md' for old,target in sorted(legacy['documents'].items()))+'\n'
    (out/'apache-legacy-redirects.conf').write_text(rules,encoding='utf8',newline='\n')
    (book/'book.json').write_text(json.dumps({**m,'hashes':hashes},ensure_ascii=False,indent=2)+'\n',encoding='utf8',newline='\n')
    (out/'index.html').write_text('<!doctype html><html lang="ja"><meta charset="utf-8"><title>Liner Notesマニュアル</title><meta http-equiv="refresh" content="0;url=manual/index.html"><a href="manual/index.html">マニュアルを開く</a></html>\n',encoding='utf8',newline='\n')
    hashes={p.relative_to(out).as_posix():sha(p) for p in sorted(out.rglob('*')) if p.is_file()}
    (out/'bundle-hashes.json').write_text(json.dumps(hashes,ensure_ascii=False,indent=2)+'\n',encoding='utf8',newline='\n')
    print(json.dumps({'pages':len(pages),'chapters':len(m['chapters']),'legacy_documents':len(legacy['documents']),'search_sections':len(entries),'output':str(out)},ensure_ascii=False))
    return m
def archive(out,m):
    target=out.parent/('liner-notes-'+m['version']+'-manual-'+m['revision'].replace('-','')+'.zip')
    with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted(out.rglob('*')):
            if p.is_file():
                zi=zipfile.ZipInfo(out.name+'/'+p.relative_to(out).as_posix(),tuple(map(int,m['revision'].split('-')))+(0,0,0));zi.compress_type=zipfile.ZIP_DEFLATED;zi.external_attr=0o100644<<16
                z.writestr(zi,p.read_bytes())
    with zipfile.ZipFile(target) as z:
        assert z.testzip() is None
        for p in out.rglob('*'):
            if p.is_file():assert z.read(out.name+'/'+p.relative_to(out).as_posix())==p.read_bytes()
    print(json.dumps({'zip':str(target),'sha256':sha(target)}))
if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('output',type=Path);ap.add_argument('--zip',action='store_true');a=ap.parse_args()
    m=build(a.output.resolve())
    if a.zip:archive(a.output.resolve(),m)
