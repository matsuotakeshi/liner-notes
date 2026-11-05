#!/usr/bin/env python3
"""Check the book, source coverage, redirects and every local URL/fragment."""
from pathlib import Path
import argparse,hashlib,json,re,sys
from html.parser import HTMLParser
from urllib.parse import unquote,urlsplit
from build import ROOT,DOC,convert,sha

class Page(HTMLParser):
    def __init__(self,s):
        super().__init__(convert_charrefs=True);self.ids=set();self.duplicates=[];self.links=[];self.feed(s)
    def handle_starttag(self,tag,attrs):
        a=dict(attrs)
        if a.get('id'):
            if a['id'] in self.ids:self.duplicates.append(a['id'])
            self.ids.add(a['id'])
        for key in ('href','src'):
            if a.get(key):self.links.append(a[key])

def check(out):
    errors=[];m=json.loads((DOC/'manifest.json').read_text(encoding='utf8'));book=out/'manual'
    fail=lambda text:errors.append(text)
    if (DOC/'LICENSE').read_bytes()!=(ROOT/'LICENSE').read_bytes():fail('Canonical license copy differs')
    if set(p['chapter'] for p in m['pages'])!=set(range(1,8)):fail('Seven chapters required')
    if not re.search(r'DISTVERSION=\s*'+re.escape(m['version'])+r'\s', (ROOT/'ports/comms/liner-notes/Makefile').read_text(encoding='utf8')):fail('Stable version differs from ports')
    inventories=ROOT/'docs/development/manual-consolidation/inventory.json'
    if inventories.exists():
        inventory=json.loads(inventories.read_text(encoding='utf8'))
        known={x['source'] for x in inventory['documents']}
        for p in ROOT.glob('*.md'):
            if p.name not in known and p.name not in ('README.md','CHANGELOG.md','AGENTS.md'):fail('Unclassified root document: '+p.name)
        for d in inventory['documents']:
            if not d['destinations']:fail('Missing document destination: '+d['source'])
            original=ROOT/d['archive']
            if not original.exists() or sha(original)!=d['sha256']:fail('Original archive differs: '+d['source'])
            for x in d['destinations']:
                if not (DOC/(x+'.md')).exists():fail('Unknown inventory destination: '+x)
        for p in (ROOT/'help/ja').glob('*.txt'):
            if p.as_posix().removeprefix(ROOT.as_posix()+'/') not in inventory['help_topics']:fail('Unclassified HELP topic: '+p.name)
    else:fail('Missing migration inventory')
    sources=json.loads((ROOT/'docs/development/manual-consolidation/source-lock.json').read_text(encoding='utf8'))
    for name,digest in sources.items():
        p=ROOT/name
        if not p.exists() or sha(p)!=digest:fail('Source changed: review manual and refresh source-lock: '+name)
    allpages={p.resolve():Page(p.read_text(encoding='utf8')) for p in out.rglob('*.html')}
    linkcount=0
    for p,parsed in allpages.items():
        if parsed.duplicates:fail(str(p)+': duplicate IDs '+str(parsed.duplicates))
        for href in parsed.links:
            u=urlsplit(href)
            if u.scheme or u.netloc:continue
            q=(p.parent/unquote(u.path)).resolve() if u.path else p
            if q.is_dir():q=q/'index.html'
            linkcount+=1
            if not q.is_relative_to(out.resolve()):fail(str(p)+': link escapes bundle '+href)
            elif not q.exists():fail(str(p)+': missing '+href)
            elif u.fragment and q in allpages and unquote(u.fragment) not in allpages[q].ids:fail(str(p)+': missing anchor '+href)
    for p in m['pages']:
        name=p['id'];text=(DOC/(name+'.md')).read_text(encoding='utf8')
        if re.search(r'(?im)^#+[^\n]*(?:stage\s*\d|第\s*\d+\s*段階)',text):fail('Historical heading in '+name)
        if re.search(r'(?im)(?:STAGE\d+\.md|evidence/stage|^#+\s*(?:検証結果|検証記録|開発成果))',text):fail('Historical publication leak in '+name)
        for s in p['sources']:
            if not (ROOT/s).exists():fail('Missing evidence source '+s)
        if (book/(name+'.md')).read_bytes()!=(DOC/(name+'.md')).read_bytes():fail('Markdown mismatch '+name)
        content=(book/(name+'.html')).read_text(encoding='utf8')
        match=re.search(r'<article id="manual-content">(.*?)</article>',content,re.S)
        if not match or match[1]!=convert(text):fail('Rendered content mismatch '+name)
        if '文書改訂 '+m['revision'] not in content:fail('Wrong revision '+name)
        for href in re.findall(r'(?<!!)\[[^\]]*\]\(([^\s)]+)\)',text):
            u=urlsplit(href)
            if u.scheme or u.netloc:continue
            q=(DOC/unquote(u.path)).resolve() if u.path else DOC/(name+'.md')
            if not q.exists():fail('Missing Markdown link in '+name+': '+href)
            elif u.fragment and q.suffix=='.md' and unquote(u.fragment) not in Page(convert(q.read_text(encoding='utf8'))).ids:fail('Missing Markdown anchor in '+name+': '+href)
    search=(book/'search-data.js').read_text(encoding='utf8')
    entries=json.loads(search.removeprefix('window.LINER_MANUAL_SEARCH=').rstrip('\n;'))
    current={p['id']+'.html' for p in m['pages']}
    for x in entries:
        u=urlsplit(x['url']);p=(book/u.path).resolve()
        if u.path not in current:fail('Non-current search result '+x['url'])
        elif u.fragment and u.fragment not in allpages[p].ids:fail('Search anchor missing '+x['url'])
    redirects=json.loads((book/'redirects.json').read_text(encoding='utf8'))
    fragments=0;legacy_anchor_entries=0
    for x in redirects:
        legacy_anchor_entries+=len(x['fragments'])
        if not (book/x['old']).exists():fail('Missing legacy URL '+x['old'])
        for dest in [x['new'],*x['fragments'].values()]:
            u=urlsplit(dest);q=(book/u.path).resolve();fragments+=bool(u.fragment)
            if not q.exists():fail('Broken redirect '+dest)
            elif u.fragment and q in allpages and u.fragment not in allpages[q].ids:fail('Broken redirect anchor '+dest)
    # Historical claims must not regress into current instructions.
    combined='\n'.join((DOC/(p['id']+'.md')).read_text(encoding='utf8') for p in m['pages'])
    for forbidden in ('CHECK MODE AUTO','GATHERREAD','TALKOFF','TALKON','本文全体はCRLF改行を含めて16384','SIGOPは担当ボードだけを管理でき、子へ権限を継承しません'):
        if forbidden in combined:fail('Obsolete/invalid claim: '+forbidden)
    lock=json.loads((out/'bundle-hashes.json').read_text(encoding='utf8'))
    actual={p.relative_to(out).as_posix():sha(p) for p in out.rglob('*') if p.is_file() and p.name!='bundle-hashes.json'}
    if actual!=lock:fail('Bundle hashes differ')
    report=dict(ok=not errors,version=m['version'],revision=m['revision'],canonical_pages=len(m['pages']),chapters=7,html_pages=len(allpages),local_links_checked=linkcount,legacy_urls=len(redirects),legacy_anchor_entries=legacy_anchor_entries,legacy_target_fragments_checked=fragments,search_sections=len(entries),markdown_html_parity=True if not errors else None,errors=errors)
    report['bundle_hashes_sha256']=sha(out/'bundle-hashes.json')
    print(json.dumps(report,ensure_ascii=False,indent=2))
    return report
if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('output',type=Path);ap.add_argument('--report',type=Path);a=ap.parse_args();r=check(a.output)
    if a.report:a.report.write_text(json.dumps(r,ensure_ascii=False,indent=2)+'\n',encoding='utf8',newline='\n')
    sys.exit(0 if r['ok'] else 1)
