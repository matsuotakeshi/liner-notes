// Local and hosted editions use the same embedded index; no fetch/file:// CORS.
const input=document.getElementById('manual-search');
if(input){
  const list=document.getElementById('manual-search-results');
  const status=document.getElementById('manual-search-result');
  const index=window.LINER_MANUAL_SEARCH||[];
  input.addEventListener('input',()=>{
    const terms=input.value.toLocaleLowerCase().trim().split(/\s+/).filter(Boolean);
    list.replaceChildren();list.hidden=!terms.length;
    if(!terms.length){status.textContent='';return;}
    const hits=index.filter(x=>terms.every(t=>(x.title+' '+x.text).toLocaleLowerCase().includes(t)));
    hits.sort((a,b)=>terms.filter(t=>b.title.toLocaleLowerCase().includes(t)).length-terms.filter(t=>a.title.toLocaleLowerCase().includes(t)).length);
    status.textContent=hits.length+'件の項目が見つかりました。'+(hits.length>40?' 先頭40件を表示します。':'');
    for(const x of hits.slice(0,40)){
      const li=document.createElement('li'),a=document.createElement('a'),p=document.createElement('p');
      a.href=x.url;a.textContent=x.title;
      const lower=x.text.toLocaleLowerCase(),at=Math.max(0,lower.indexOf(terms[0])-35);
      p.textContent=(at?'…':'')+x.text.slice(at,at+160)+'…';li.append(a,p);list.append(li);
    }
  });
}

if (navigator.clipboard?.writeText) {
  const japanese = document.documentElement.lang === 'ja';
  for (const code of document.querySelectorAll('.manual-article pre > code')) {
    const button = document.createElement('button');
    const label = japanese ? 'コードをコピー' : 'Copy code';
    button.type = 'button';
    button.className = 'manual-copy';
    button.textContent = label;
    button.addEventListener('click', async () => {
      try {
        await navigator.clipboard.writeText(code.textContent);
        button.textContent = japanese ? 'コピーしました' : 'Copied';
      } catch {
        button.textContent = japanese ? 'コピーできませんでした' : 'Copy failed';
      }
      setTimeout(() => { button.textContent = label; }, 1600);
    });
    code.parentElement.before(button);
  }
}
