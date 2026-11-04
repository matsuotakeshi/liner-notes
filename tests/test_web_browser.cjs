const {chromium}=require('playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const url=process.argv[2]||'http://127.0.0.1:18889/notes/';
const suffix=Date.now().toString();
const out=path.resolve(process.argv[3]||'web-browser-results');fs.mkdirSync(out,{recursive:true});
(async()=>{
 const browser=await chromium.launch({...(process.env.LN_BROWSER_CHANNEL?{channel:process.env.LN_BROWSER_CHANNEL}:{}),headless:true});const context=await browser.newContext({viewport:{width:1440,height:1000},acceptDownloads:true});const page=await context.newPage();const errors=[];page.on('pageerror',e=>errors.push(String(e)));
 const nav=name=>page.locator('#navigation').getByRole('button',{name,exact:false}).click();
 const close=()=>page.locator('#dialog-close').click();
 try{
  await page.goto(url);await page.locator('.hero').waitFor();
  assert(await page.locator('#login-button').isVisible());assert.equal(await page.locator('#navigation').getByRole('button',{name:/ゲーム/}).count(),0);
  await page.locator('#new-post').click();await page.locator('#dialog[open] .access-notice').waitFor();assert.equal(await page.locator('#dialog textarea').count(),0);await close();
  await nav('チャット・トーク 🔒');await page.locator('#dialog[open] .access-notice').waitFor();assert.equal(await page.locator('.chat-log').count(),0);await close();
  await page.locator('#login-button').click();await page.locator('#dialog input[name=login]').fill('alice');await page.locator('#dialog input[name=password]').fill('browser-test-password');await page.locator('#dialog button[type=submit]').click();await page.locator('#dialog').waitFor({state:'hidden'});
  await page.waitForFunction(()=>document.querySelector('#account-name').textContent==='alice');assert(!await page.locator('#login-button').isVisible());
  await nav('掲示板');await page.locator('.board-picker').getByRole('button',{name:/Child board/}).click();await page.locator('#breadcrumb').getByRole('button',{name:'Child board',exact:true}).waitFor();
  await page.locator('#new-post').click();assert.equal(await page.locator('#dialog select[name=board]').inputValue(),'2');assert.equal(await page.locator('#dialog select[name=board] option').count(),2);
  await page.locator('#dialog input[name=title]').fill('Browser note '+suffix);await page.locator('#dialog textarea[name=body]').fill('A child board post.');await page.locator('#dialog button[type=submit]').click();await page.locator('#dialog').waitFor({state:'hidden'});
  await page.locator('#main').getByRole('button',{name:new RegExp('Browser note '+suffix)}).click();await page.locator('.article').waitFor();await page.locator('#breadcrumb').getByRole('button',{name:'General',exact:true}).click();await page.locator('.board-layout').waitFor();
  await nav('会員・プロフィール');await page.getByRole('button',{name:'項目を編集',exact:false}).click();await page.locator('.profile-editor').waitFor();assert.equal(await page.locator('.profile-editor [data-fields] label').count(),13);assert.equal(await page.locator('.profile-editor select').count(),0);
  await page.locator('.profile-editor input[name=LOCATION]').fill('Tokyo');await page.locator('.profile-editor textarea[name=BIO]').fill('First line\nSecond line');await page.locator('.profile-editor button[type=submit]').click();await page.locator('#dialog').waitFor({state:'hidden'});
  await page.getByRole('button',{name:'項目を編集',exact:false}).click();assert.equal(await page.locator('.profile-editor input[name=LOCATION]').inputValue(),'Tokyo');assert((await page.locator('.profile-editor textarea[name=BIO]').inputValue()).includes('Second line'));await close();
  // Candidate rendering uses a controlled response; actual offset/owner recovery is tested in test_web_transfers.py.
  await page.route('**/api/resumes',route=>route.fulfill({json:{uploads:[{id:321,kind:'public',name:'interrupted.bin',size:70000,category:'-',description:'Saved explanation',active:false}]}}));
  await nav('ファイル保管庫');await page.getByRole('button',{name:/interrupted.bin/}).click();assert.equal(await page.locator('[name=resume]').getAttribute('type'),'hidden');assert.equal(await page.locator('[name=resume]').inputValue(),'321');assert.equal(await page.locator('[name=description]').inputValue(),'Saved explanation');assert((await page.locator('[data-resume-selected]').textContent()).includes('interrupted.bin'));
  await page.unroute('**/api/resumes');
  await nav('メール');await page.locator('.page-header').getByRole('button',{name:/投稿する/}).click();await page.locator('#dialog input[name=recipient]').fill('alice');await page.locator('#dialog input[name=title]').fill('Browser attachment '+suffix);await page.locator('#dialog textarea[name=body]').fill('Attached here.');
  const payload=Buffer.alloc(100000);for(let i=0;i<payload.length;i++)payload[i]=i%256;
  await page.locator('#dialog input[type=file]').setInputFiles({name:'browser-添付.bin',mimeType:'application/octet-stream',buffer:payload});
  assert.equal(await page.locator('#dialog input[name=attachments]').count(),0);await page.locator('#dialog button[type=submit]').click();await page.locator('#dialog').waitFor({state:'hidden',timeout:60000});await page.locator('#main').getByRole('button',{name:new RegExp('Browser attachment '+suffix)}).click();
  const dl=page.waitForEvent('download');await page.getByRole('button',{name:'↓ browser-添付.bin',exact:true}).click();const download=await dl;const target=path.join(out,'browser-attachment.bin');await download.saveAs(target);assert(fs.readFileSync(target).equals(payload));
  await nav('管理');assert.equal(await page.locator('.operation-sections > section').count(),5);
  await page.getByRole('button',{name:'接続管理',exact:false}).click();await page.locator('.report-table').waitFor();
  assert((await page.locator('.report-table tbody tr').count())>=1);
  const search=page.locator('.report-view input[type=search]');await search.fill('no-such-user');assert.equal(await page.locator('.report-table tbody tr').count(),0);await search.fill('');
  await page.screenshot({path:path.join(out,'admin-connections.png'),fullPage:true});
  await nav('個人設定');await page.locator('.report-view dl').waitFor();
  await nav('ホーム');await page.getByRole('button',{name:'管理者へ連絡',exact:true}).click();await page.locator('#dialog input[name=title]').fill('Browser SYSOP contact');await page.locator('#dialog button[type=submit]').click();await page.locator('#dialog textarea[name=body]').waitFor();await page.locator('#dialog textarea[name=body]').fill('Return to home.');await page.locator('#dialog button[type=submit]').click();await page.locator('#dialog').waitFor({state:'hidden'});await page.locator('.hero').waitFor();
  await nav('チャット・トーク');await page.locator('.chat-form input').fill('Left aligned message');await page.locator('.chat-form button[type=submit]').click();await page.locator('.chat-message.own').waitFor();
  assert.equal(await page.locator('.chat-message.own').evaluate(e=>getComputedStyle(e).alignSelf),'flex-start');await page.locator('.page-header').getByRole('button',{name:'その他の操作'}).click();await page.locator('#dialog').getByRole('button',{name:/^状態/}).click();await page.locator('#chat-results .result').waitFor();
  const result=await page.locator('#chat-results').boundingBox(),chat=await page.locator('.chat-log').boundingBox();assert(result.y+result.height<=chat.y);
  await page.screenshot({path:path.join(out,'chat-desktop.png'),fullPage:true});
  page.once('dialog',d=>d.accept());await nav('端末');await page.locator('.xterm-screen').waitFor();await page.waitForTimeout(500);
  for(const size of [{width:1440,height:1000},{width:390,height:844}]){await page.setViewportSize(size);await page.waitForTimeout(500);const outer=await page.locator('.browser-terminal').boundingBox(),inner=await page.locator('.xterm-screen').boundingBox();assert(inner.y+inner.height<=outer.y+outer.height+1,JSON.stringify({outer,inner}));assert(inner.x+inner.width<=outer.x+outer.width+1,JSON.stringify({outer,inner}));await page.screenshot({path:path.join(out,'terminal-'+size.width+'.png'),fullPage:true});}
  assert.deepEqual(errors,[]);console.log('PASS browser: visible login, guest preflight, empty games hidden, board picker/breadcrumbs, all profile fields + persistence, named resume selection, inline mail upload/download, SYSOP return home, left chat/results placement, desktop/mobile terminal bounds');
 }catch(error){await page.screenshot({path:path.join(out,'browser-failure.png'),fullPage:true});console.error('URL',page.url(),'PAGE ERRORS',errors);throw error;}finally{await context.close();await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
