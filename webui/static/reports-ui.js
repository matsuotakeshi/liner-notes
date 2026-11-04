// Report rendering never inserts HTML supplied by members or the BBS.
const names={connection:['接続番号','Connection'],login:['ログイン名','Login'],state:['状態','State'],transport:['回線','Transport'],peer:['接続元','Peer'],seconds:['利用秒数','Seconds'],idle:['無入力秒数','Idle seconds'],received:['受信バイト','Received bytes'],sent:['送信バイト','Sent bytes'],queued:['送信待ち','Queued bytes'],columns:['幅','Columns'],rows:['高さ','Rows'],id:['番号','ID'],user:['会員','Member'],event:['操作','Event'],timestamp:['日時','Time'],file:['ファイル','File'],bytes:['容量','Bytes'],protocol:['転送方式','Protocol'],downloads:['取得回数','Downloads'],article:['記事','Article'],board:['ボード','Board'],status:['状態','Status'],logins:['ログイン回数','Logins']};
export function reportView(report,{el,button,lang,action,more,available}){
  const extra={level:['会員レベル','Level'],groups:['所属グループ','Groups'],language:['言語','Language'],pager:['ページ停止','Paging'],login_at:['ログイン','Login time'],logout_at:['ログアウト','Logout time'],date:['日時','Date'],target:['対象','Target'],before:['変更前','Before'],after:['変更後','After'],description:['内容','Description']};
  const label=key=>(names[key]||extra[key])?.[lang==='en'?1:0]||key;
  const root=el('section',{class:'card result report-view'},el('h2',{},report.title));
  if(report.kind==='fields'){
    root.append(el('dl',{class:'profile-fields'},...report.fields.flatMap(([k,v])=>[el('dt',{},label(k)),el('dd',{},v)])));
    return root;
  }
  const body=el('tbody');
  const table=el('table',{class:'report-table'},el('thead',{},el('tr',{},...report.columns.map(c=>el('th',{scope:'col'},label(c))),el('th',{scope:'col'},lang==='ja'?'操作':'Actions'))),body);
  const search=el('input',{type:'search','aria-label':lang==='ja'?'結果の絞り込み':'Filter results',placeholder:lang==='ja'?'このページ内を検索':'Filter this page'});
  const fill=()=>{
    body.replaceChildren(...report.rows.filter(row=>row.join(' ').toLowerCase().includes(search.value.toLowerCase())).map(row=>{
      const values=Object.fromEntries(report.columns.map((c,i)=>[c,row[i]]));
      const controls=[];
      const offer=(id,text,params)=>{if(available(id))controls.push(button(text,()=>action(id,params),'text-button'));};
      if(values.connection&&report.title==='CONNECTIONS')offer('admin.disconnect',lang==='ja'?'切断':'Disconnect',{id:values.connection});
      if(values.file)offer('file.detail',lang==='ja'?'詳細':'Details',{id:values.file});
      if(values.user&&values.user!=='0')offer('admin.rights',lang==='ja'?'会員権限':'Member rights',{user:values.user});
      if(report.title==='MEMBERS'&&values.status==='pending')offer('admin.application',lang==='ja'?'入会申請':'Application',{user:values.user});
      if(values.article)offer('board.tree',lang==='ja'?'記事ツリー':'Thread',{id:values.article});
      if(values.board)offer('admin.board_info',lang==='ja'?'設定':'Settings',{id:values.board});
      return el('tr',{},...row.map((value,i)=>el('td',{},['timestamp','login_at','logout_at'].includes(report.columns[i])&&/^\d+$/.test(value)&&+value>0?new Date(+value*1000).toLocaleString(lang==='ja'?'ja-JP':'en-US'):value)),el('td',{},controls));
    }));
  };
  search.addEventListener('input',fill);fill();
  root.append(search,el('div',{class:'report-scroll'},table));
  if(!report.rows.length)root.append(el('p',{},lang==='ja'?'該当する記録はありません。':'No matching records.'));
  if(report.next)root.append(button(lang==='ja'?'続きを表示':'Show more',more));
  if(report.details?.length)root.append(el('details',{},el('summary',{},lang==='ja'?'補足':'Details'),...report.details.map(text=>el('p',{},text))));
  return root;
}

export function actionCategory(id){
  const key=id.replace(/^admin\./,'');
  if(/^(connections|disconnect|announce|shutdown)/.test(key))return 'connections';
  if(/^(file_|file\.)/.test(key))return 'files';
  if(/^(board_|sigop|author|hide|restore|inspect|move|audit)/.test(key))return 'board';
  if(/^(userlog|userstats)/.test(key))return 'history';
  return 'people';
}
