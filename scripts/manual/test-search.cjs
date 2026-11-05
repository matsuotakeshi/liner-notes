// Pure JavaScript regression checks, not a browser/rendering test.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const root=path.resolve(process.argv[2]);
class Element{
  constructor(){this.children=[];this.textContent='';this.hidden=false;this.value='';this.events={};}
  replaceChildren(){this.children=[];}
  append(...children){this.children.push(...children);}
  addEventListener(name,fn){this.events[name]=fn;}
}
const fields={'manual-search':new Element(),'manual-search-results':new Element(),'manual-search-result':new Element()};
const context={window:{},document:{getElementById:id=>fields[id],createElement:()=>new Element()}};
vm.createContext(context);
vm.runInContext(fs.readFileSync(path.join(root,'manual/search-data.js'),'utf8'),context);
vm.runInContext(fs.readFileSync(path.join(root,'assets/manual.js'),'utf8'),context);
const input=fields['manual-search'],list=fields['manual-search-results'],status=fields['manual-search-result'];
function search(s){input.value=s;input.events.input();return list.children.map(x=>x.children[0].href);}
const results=[];
for(const term of ['ZRESUME','SIGOP','添付 再開','DB','INSTALL']){
  const hits=search(term);assert(hits.length>0,term);assert(hits.length<=40);
  assert(hits.every(x=>!/^STAGE\d|^[A-Z_]+\.html/.test(x)),'Historical result leaked');
  results.push({term,visible:hits.length,status:status.textContent});
}
assert.deepEqual(search('zresume'),search('ZRESUME'));
assert.equal(search('no-match-unique-9172654').length,0);assert(status.textContent.startsWith('0件'));
search('  ');assert.equal(list.hidden,true);assert.equal(status.textContent,'');
console.log(JSON.stringify({ok:true,kind:'JavaScript logic only; no browser rendering',cases:results.length+3,results},null,2));
