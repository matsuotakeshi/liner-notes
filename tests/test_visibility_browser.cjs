const {chromium}=require('playwright');
const assert=require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({headless:true});
 const page=await browser.newPage();
 const errors=[];page.on('pageerror',e=>errors.push(String(e)));
 try{
  await page.goto(process.argv[2]);await page.locator('.hero').waitFor();
  await page.locator('#login-button').click();
  await page.locator('#dialog input[name=login]').fill('alice');
  await page.locator('#dialog input[name=password]').fill('browser-test-password');
  await page.locator('#dialog button[type=submit]').click();
  await page.locator('#dialog').waitFor({state:'hidden'});
  async function request(route,payload){return page.evaluate(async({route,payload})=>{
   const session=await (await fetch('/notes/api/session')).json();
   const r=await fetch('/notes/api/'+route,{method:'POST',headers:{'Content-Type':'application/json','X-CSRF-Token':session.csrf},body:JSON.stringify(payload)});
   if(!r.ok)throw Error(await r.text());return r.json();
  },{route,payload});}
  await request('compose',{kind:'article',title:'needle-root',body:'needle-root-body'});
  await request('compose',{kind:'article',parent:1,title:'needle-child',body:'needle-child-body'});
  await page.locator('#navigation').getByRole('button',{name:/掲示板/}).click();
  await page.locator('#main').getByRole('button',{name:/needle-root/}).click();
  await page.locator('.article').waitFor();
  await page.getByRole('button',{name:'返信ツリー',exact:true}).click();
  await page.locator('#main .result').getByRole('button',{name:/needle-child/}).waitFor();
  const hidden=await request('input',{line:'HIDE 1'});assert.match(hidden.text,/HIDDEN SAVED 1/);
  // A previously rendered tree is stale: selecting its child must clear the
  // old article and result cards after the core refuses the new read.
  await page.locator('#main .result').getByRole('button',{name:/needle-child/}).click();
  await page.waitForFunction(()=>document.querySelector('#main').textContent.includes('HIDDEN 2'));
  assert.equal(await page.locator('.article').count(),0);
  assert(!/needle/.test(await page.locator('#main').textContent()));
  assert.deepEqual(errors,[]);
  console.log('PASS browser: hidden descendant read clears stale article and tree; no title/body preview remains');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
