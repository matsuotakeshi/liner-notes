"""Declarative Web forms. Commands execute as the current BBS member, never SYSOP impersonation."""
from .bridge import BridgeError

COMMANDS = {}


def add(key, group, label, command, fields=(), *, role='member', confirm=False, edit=False):
    COMMANDS[key] = dict(id=key, group=group, label=label, command=command,
                         fields=[{'name':x[0], 'label':x[1], 'default':x[2] if len(x)>2 else ''} for x in fields],
                         role=role, confirm=confirm, edit=edit)


F_ID = ('id', '番号 / ID')
F_USER = ('user', '会員番号 / Member ID')
F_PATH = ('path', 'ファイルボード / File board', '-')
F_TEXT = ('text', '内容 / Text')
F_QUERY = ('query', '検索語 / Search')
F_FIELD = ('field', '項目 / Field')

add('board.info','board','ボード案内 / Board information','INFO',role='guest')
add('board.tree','board','返信ツリー / Reply tree','TREE {id}',[F_ID],role='guest')
add('board.unread','board','未読記事 / Unread','UNREAD')
add('board.new','board','次の未読を読む / Read next unread','NEW')
add('board.next_unread_board','board','次の未読ボード / Next unread board','NEXTBOARD')
add('board.unread_threads','board','未読のあるスレッド / Unread threads','NOTES UNREAD')
add('board.search','board','検索 / Search','FIND {scope} * {query}',[('scope','範囲 / Scope','ALL'),F_QUERY])
add('board.author','board','投稿者で検索 / By author','FIND {scope} {author} *',[('author','ログイン名 / Login'),('scope','範囲 / Scope','ALL')])
add('board.news','board','日付で検索 / Since date','NEWS {date}',[('date','日付 YYYY-MM-DD / Date')])
add('board.check','board','新着巡回 / Check new','CHECK ARTICLES {scope}',[('scope','HERE / SUBTREE / ALL','HERE')])
add('check.next','board','巡回の続き / Continue check','CHECK NEXT')
add('check.done','board','確認日時を保存 / Finish check','CHECK DONE')
add('check.cancel','board','巡回を中止 / Cancel check','CHECK CANCEL')
add('board.seen','board','既読にする / Mark read','SEEN {id}',[F_ID])
add('board.unseen','board','未読に戻す / Mark unread','UNSEEN {id}',[F_ID])
add('board.get','board','共通バッファへ / Copy to buffer','GET {id}',[F_ID])
add('board.retitle','board','題名を変更 / Rename article','RETITLE {id} {text}',[F_ID,F_TEXT])
add('board.withdraw','board','記事を取り消す / Withdraw','WITHDRAW {id} CONFIRM',[F_ID],confirm=True)
add('board.draft','board','保存した下書き / Saved draft','DRAFT')
add('board.resume','board','下書きを再開 / Resume draft','RESUME',edit=True)
add('board.draft_delete','board','下書きを削除 / Delete draft','DRAFT DELETE {revision}',[('revision','版番号 / Revision')],confirm=True)
add('board.unlock','board','ボードのパスワード / Unlock board','BOARD {id}',[F_ID],role='guest')

for key,label,cmd in [('inbox','受信 / Inbox','MAIL INBOX'),('sent','送信 / Sent','MAIL SENT'),('unread','未読 / Unread','MAIL UNREAD'),('draft','下書き / Draft','MAIL DRAFT'),('attachments','添付一覧 / Attachments','MAIL FILES')]:
    add('mail.'+key,'mail',label,cmd)
add('mail.search','mail','本文検索 / Search mail','MAIL SEARCH {query}',[F_QUERY])
add('mail.news','mail','日付で検索 / Since date','MAIL NEWS {date}',[('date','日付 YYYY-MM-DD / Date')])
add('mail.check','mail','新着巡回 / Check new','CHECK MAIL')
add('mail.get','mail','共通バッファへ / Copy to buffer','MAIL GET {id}',[F_ID])
add('mail.resume','mail','下書きを再開 / Resume draft','MAIL RESUME',edit=True)
add('mail.draft_delete','mail','下書きを削除 / Delete draft','MAIL DRAFT DELETE {revision}',[('revision','版番号 / Revision')],confirm=True)
add('mail.delete','mail','メールを削除 / Delete message','MAIL DELETE {id}',[F_ID],confirm=True)
add('mail.drop','mail','未使用添付を回収 / Remove attachment','MAIL DROP {id} CONFIRM',[F_ID],confirm=True)
add('mail.notify','settings','新着通知 / Mail notifications','MAIL NOTIFY {value}',[('value','ON / OFF','ON')])
add('mail.sysop','mail','管理者へメール / Contact SYSOP','SYSOPMAIL {title}',[('title','題名 / Subject')],role='guest',edit=True)

for key,label,cmd in [('boards','ファイルボード / File boards','FILEBOARDS'),('list','ファイル一覧 / Files','FILES'),('stats','取得回数 / Download counts','FILESTATS'),('history','取得履歴 / Download history','FILEHISTORY'),('audit','変更履歴 / File activity','FILEAUDIT')]:
    add('file.'+key,'files',label,cmd)
for key,label,cmd,fields in [
    ('info','ボード案内 / Board information','FILEBOARDINFO {path}',[F_PATH]),
    ('select','ボードを選ぶ / Select board','FILEBOARD {path}',[F_PATH]),
    ('children','子ボード / Child boards','FILECHILDREN {path}',[F_PATH]),
    ('subtree','配下の一覧 / Subtree','FILESIN {path}',[F_PATH]),
    ('search','名前で検索 / Search files','FILESEARCH {query}',[F_QUERY]),
    ('detail','ファイル詳細 / File details','FILE {id}',[F_ID]),
    ('text','テキスト表示 / Read text file','FILETYPE {id}',[F_ID]),
    ('desc','説明を編集 / Edit description','FILEDESC {id} {text}',[F_ID,F_TEXT]),
    ('rename','ファイル名を変更 / Rename file','FILERENAME {id} {text}',[F_ID,F_TEXT]),
    ('category','分類を変更 / Change category','FILECATEGORY {id} {path}',[F_ID,F_PATH]),
    ('ascii','テキスト属性 / Text attribute','FILEASCII {id} {value}',[F_ID,('value','ON / OFF','ON')]),
    ('copy','ボードへコピー / Copy to board','FILECOPY {id} {path}',[F_ID,F_PATH]),
    ('check','新着巡回 / Check new files','CHECK FILES {path}',[F_PATH]),
    ('batch_add','一括選択へ追加 / Add selection','BATCH ADD {id}',[F_ID]),
    ('batch_remove','選択を解除 / Remove selection','BATCH REMOVE {id}',[F_ID]),
    ('batch_match','ワイルドカード選択 / Match files','BATCH MATCH {text}',[('text','パターン / Pattern','*.zip')]),
    ('batch_list','選択一覧 / Selected files','BATCH LIST',[]),
    ('batch_clear','選択をクリア / Clear selection','BATCH CLEAR',[])]:
    add('file.'+key,'files',label,cmd,fields)
for key,label,cmd in [('delete','ファイルを削除 / Delete file','FILEDELETE'),('hide','非公開にする / Hide file','FILEHIDE'),('restore','再公開 / Publish again','FILERESTORE')]:
    add('file.'+key,'files',label,cmd+' {id} CONFIRM',[F_ID],confirm=True)

add('profile.self','people','自分のプロフィール / My profile','PROFILE')
add('profile.list','people','会員一覧 / Members','PROFILES')
add('profile.search','people','プロフィール検索 / Search profiles','PROFILES {query}',[F_QUERY])
add('profile.find','people','項目で検索 / Search field','PROFILEFIND {field} {query}',[F_FIELD,F_QUERY])
add('profile.group','people','所属グループで検索 / Search group','PROFILEFIND GROUP {group}',[('group','グループ番号 / Group ID')])
add('profile.view','people','プロフィールを見る / View profile','PROFILE {login}',[('login','ログイン名 / Login')])
add('profile.set','people','項目を編集 / Edit profile','PROFILESET {field} {text}',[F_FIELD,F_TEXT])
add('profile.visibility','people','公開範囲 / Visibility','PROFILEVIS {field} {value}',[F_FIELD,('value','PRIVATE / MEMBERS / MONTHDAY','PRIVATE')])
add('profile.bio','people','長文自己紹介 / Biography','BIOEDIT',edit=True)
add('profile.handle','people','ハンドル / Handle','HANDLE {text}',[F_TEXT],role='guest')
add('online','people','接続者 / Online members','ONLINE')
add('memo','people','接続者メモ / Connection memo','MEMO {text}',[F_TEXT])
add('call','people','SYSOP呼び出し / Call SYSOP','CALL',role='guest')
add('userlog','people','接続履歴 / Connection history','USERLOG')
add('userstats','people','利用時間 / Usage statistics','USERSTATS')
add('usertable','people','接続履歴の表 / Session table','USERTABLE')
add('talk','chat','個別トーク / Private talk','TALK {id}',[('id','接続番号 / Connection ID')])
add('chat.open','chat','チャットへ入る / Join chat','CHAT')
add('chat.exit','chat','チャットを出る / Leave chat','/LEAVE')
add('chat.online','chat','接続者 / Online','W')
add('chat.status','chat','状態 / Status','#')
add('chat.memo','chat','メモ / Memo','``')
add('chat.call','chat','SYSOP呼び出し / Call SYSOP','D')
add('chat.kick','chat','チャットから切断 / Kick','/KICK {login}',[('login','ログイン名 / Login')],role='admin',confirm=True)
add('gather.open','gather','伝言を読む・書く / Read and write','GATHER')
add('gather.read','gather','伝言を読む / Read messages','GATHER READ')
add('file.unlock','files','保管庫のパスワード / Unlock file board','FILEUNLOCK {path}',[F_PATH])
add('file.news','files','配下の新着 / New files in subtree','FILENEWS {path}|{date}',[F_PATH,('date','YYYY-MM-DD')])
add('file.search_in','files','配下を検索 / Search subtree','FILESEARCH {path}|{query}',[F_PATH,F_QUERY])
add('file.upload_status','files','中断した登録の状態 / Upload status','FILESTATUS {id}',[F_ID])
add('file.upload_cancel','files','中断した登録を破棄 / Cancel upload','FILECANCEL {id}',[F_ID],confirm=True)
add('mail.upload_status','mail','添付の登録状態 / Attachment upload status','MFILESTATUS {id}',[F_ID])
add('mail.upload_cancel','mail','中断した添付を破棄 / Cancel attachment upload','MFILECANCEL {id}',[F_ID],confirm=True)
add('account','settings','会員情報 / Account','ACCOUNT')
add('settings.checkmode','settings','巡回日時の更新 / Check timestamp update','CHECKMODE {value}',[('value','AUTO / MANUAL','MANUAL')])
add('settings.notifications','settings','新着通知 / New notifications','NOTIFY {value}',[('value','ON / OFF','ON')])
add('settings.buffer_clear','settings','共通バッファを消去 / Clear shared buffer','BUFFER CLEAR',confirm=True)
add('file.batch_copy','files','選択ファイルをコピー / Copy selected files','BATCH COPY {path}',[F_PATH],role='vault_manager',confirm=True)
add('retire','settings','退会する / Retire membership','RETIRE CONFIRM',confirm=True)
for key,cmd in [('receive','/TALK {value}'),('mute','/MUTE {value}'),('unmute','/UNMUTE {value}')]:
    add('chat.'+key,'chat',{'receive':'トーク受信 / Receive talks','mute':'受信拒否 / Mute member','unmute':'受信拒否を解除 / Unmute member'}[key],cmd,[('value','ON / OFF' if key=='receive' else 'ログイン名 / Login')])
add('chat.preferences','chat','受信設定を確認 / Talk preferences','/MUTES')
for key,cmd,label in [('prev','PREV','前の記事 / Previous article'),('next','NEXT','次の記事 / Next article'),('parent','PARENT','親記事 / Parent article'),('child','CHILD','返信記事 / Child article')]:
    add('board.'+key,'board',label,cmd,role='guest')
add('board.scan','board','連続読み出し / Sequential reading','SCAN {order} {scope} {author} {query}',[('order','ASC / DESC','ASC'),('scope','HERE / SUBTREE / ALL','HERE'),('author','投稿者または* / Author or *','*'),('query','検索語または* / Query or *','*')])
add('mail.scan','mail','連続読み出し / Sequential mail','MAIL SCAN {order}',[('order','ASC / DESC','ASC')])
for key,cmd,label in [('prev','MAIL P','前へ / Previous'),('next','MAIL N','次の未読 / Next unread'),('repeat','MAIL L','再表示 / Read again')]:
    add('mail.'+key,'mail',label,cmd)
add('continue','help','結果の続き / Continue result','+',role='guest')
add('cancel','help','対話を中止 / Cancel dialogue','/CANCEL',role='guest')

for key,label,cmd,fields in [
    ('prefs','保存した設定 / Preferences','PREFS',[]),
    ('lang','端末の言語 / Terminal language','LANG {value}',[('value','ja / en','ja')]),
    ('pager','端末のページ停止 / Terminal paging','PAGER {value}',[('value','ON / OFF','OFF')]),
    ('dimensions','端末サイズ / Terminal dimensions','PAGER SIZE {columns} {rows}',[('columns','列 / Columns','80'),('rows','行 / Rows','24')]),
    ('editor','端末エディタ / Terminal editor','EDITOR {value}',[('value','BETA / LINE','BETA')]),
    ('menu','端末の案内量 / Terminal menu','MENUMODE {value}',[('value','BEGINNER / STANDARD / EXPERT','STANDARD')]),
    ('protocol','端末転送方式 / Terminal transfer','PROTOCOL {value}',[('value','方式 / Protocol','ZMODEM')]),
    ('save','設定を保存 / Save preferences','PREFS SAVE',[]),
    ('load','設定を再読込 / Reload preferences','PREFS LOAD',[]),
    ('password','パスワードを変更 / Change password','PASSWD',[]),
    ('buffer','共通編集バッファ / Shared buffer','BUFFER',[])]:
    add('settings.'+key,'settings',label,cmd,fields,edit=key=='buffer')
add('help','help','操作ヘルプ / Help','HELP {topic}',[('topic','項目 / Topic','READ')],role='guest')
add('games','games','ゲーム一覧 / Games','GAMES')

for key,label,cmd,fields,role,confirm in [
    ('members','会員一覧 / Members','MEMBERS',[],'admin',False),
    ('member','会員詳細 / Member','MEMBER {user}',[F_USER],'admin',False),
    ('rights','権限 / Rights','RIGHTS {user}',[F_USER],'manager',False),
    ('applications','入会申請 / Applications','APPLICATIONS',[],'admin',False),
    ('application','申請内容 / Application','APPLICATION {user}',[F_USER],'admin',False),
    ('application_set','申請を修正 / Edit application','APPLICATIONSET {user} {field} {text}',[F_USER,F_FIELD,F_TEXT],'admin',False),
    ('approve','入会を承認 / Approve','APPROVE {user}',[F_USER],'admin',True),
    ('reject','申請を却下 / Reject','REJECT {user} CONFIRM',[F_USER],'admin',True),
    ('application_delete','申請を削除 / Delete application','DELETEAPPLICATION {user} CONFIRM',[F_USER],'admin',True),
    ('suspend','利用停止 / Suspend','SUSPEND {user}',[F_USER],'admin',True),
    ('resume','停止解除 / Restore member','RESUME {user}',[F_USER],'admin',True),
    ('delete_user','会員を削除 / Delete member','DELETEUSER {user} CONFIRM',[F_USER],'admin',True),
    ('reset','パスワード再設定 / Reset password','RESETPASS {user}',[F_USER],'admin',True),
    ('level','会員レベル / Member level','LEVEL {user} {level}',[F_USER,('level','レベル 0–5 / Level','1')],'admin',True),
    ('group','グループへ追加 / Add to group','GROUP {user} {group}',[F_USER,('group','グループ 1–100 / Group')],'manager',False),
    ('ungroup','グループから除外 / Remove from group','UNGROUP {user} {group}',[F_USER,('group','グループ / Group')],'manager',True),
    ('group_move','所属を移行 / Move group','GROUPMOVE {source} {target} CONFIRM',[('source','元 / From'),('target','先 / To')],'admin',True),
    ('profile','会員プロフィール編集 / Edit member profile','MEMBERPROFILE {user} {field} {text}',[F_USER,F_FIELD,F_TEXT],'admin',False),
    ('board_info','ボード設定 / Board settings','BOARDINFO {id}',[F_ID],'manager',False),
    ('board_set','ボードを作成・変更 / Set board','BOARDSET {id}|{parent}|{read}|{write}|{title}|{description}',[F_ID,('parent','親 / Parent','0'),('read','閲覧条件 / Read','member'),('write','投稿条件 / Write','member'),('title','題名 / Title'),('description','説明 / Description')],'manager',False),
    ('board_options','ボードの属性・上限 / Board options','BOARDOPTIONS {id} {kind} {order} {count} {bytes} {keep}',[F_ID,('kind','BOARD / MENU','BOARD'),('order','配置 / Order','0'),('count','記事数 / Articles','0'),('bytes','本文容量 / Bytes','0'),('keep','ログ保持 / Keep logs','1000')],'manager',False),
    ('board_pass','ボードパスワード / Board password','BOARDPASS {id}',[F_ID],'manager',False),
    ('board_pass_clear','パスワードを解除 / Remove board password','BOARDPASS {id} NONE CONFIRM',[F_ID],'manager',True),
    ('board_guest','ゲスト投稿 / Guest posts','BOARDGUEST {id} {value}',[F_ID,('value','ON / OFF','OFF')],'manager',False),
    ('sigops','担当ボード / Assigned boards','SIGOPS',[],'manager',False),
    ('sigop','担当者を変更 / Assign SIGOP','SIGOP {id} {user}',[F_ID,('user','会員番号またはNONE / Member ID or NONE')],'manager',True),
    ('sigop_group','担当グループ / Delegate group','SIGOPGROUP {id} {group}',[F_ID,('group','グループ / Group')],'admin',True),
    ('board_group','ボードと所属を移行 / Move board group','BOARDGROUP {id} {source} {target} CONFIRM',[F_ID,('source','元グループ / From'),('target','先グループ / To')],'admin',True),
    ('author','記事投稿者を変更 / Change author','AUTHOR {id} {user} CONFIRM',[F_ID,F_USER],'manager',True),
    ('hide','記事を非表示 / Hide article','HIDE {id}',[F_ID],'manager',True),
    ('restore','記事を再表示 / Restore article','RESTORE {id}',[F_ID],'manager',True),
    ('inspect','非表示記事を確認 / Inspect article','INSPECT {id}',[F_ID],'manager',False),
    ('move','スレッドを移動 / Move thread','MOVE {id} {target} CONFIRM',[F_ID,('target','移動先ボード / Destination')],'admin',True),
    ('move_branch','枝を移動 / Move branch','MOVEBRANCH {id} {target} CONFIRM',[F_ID,('target','移動先ボード / Destination')],'admin',True),
    ('board_delete','空ボードを削除 / Delete empty board','BOARDDELETE {id} CONFIRM',[F_ID],'admin',True),
    ('board_delete_tree','ボード配下を削除 / Delete board tree','BOARDDELETEALL {id} CASCADE CONFIRM',[F_ID],'admin',True),
    ('board_log','ボード変更履歴 / Board changes','BOARDLOG',[],'manager',False),
    ('board_access','閲覧・投稿履歴 / Board access','BOARDACCESS {id}',[F_ID],'manager',False),
    ('audit','操作履歴 / Audit','AUDIT',[],'manager',False),
    ('audit_article','記事別履歴 / Article audit','AUDIT ARTICLE {id}',[F_ID],'manager',False),
    ('audit_actor','操作者別履歴 / Actor audit','AUDIT ACTOR {login}',[('login','ログイン名 / Login')],'manager',False),
    ('file_board','ファイルボード権限 / File board permissions','FILEBOARDSET {path}|{read}|{write}',[F_PATH,('read','閲覧条件 / Read','member'),('write','登録条件 / Upload','member')],'manager',False),
    ('file_name','ボード表示名 / File board name','FILEBOARDNAME {path}|{text}',[F_PATH,F_TEXT],'manager',False),
    ('file_options','ファイルボード属性 / File board options','FILEBOARDOPTIONS {path}|{kind} {order} {keep}',[F_PATH,('kind','BOARD / MENU','BOARD'),('order','配置 / Order','0'),('keep','履歴保持 / History','1000')],'manager',False),
    ('file_sigop','保管庫SIGOP / File SIGOP','FILEBOARDSIGOP {path}|{user}',[F_PATH,('user','会員番号またはNONE / Member ID or NONE')],'manager',True),
    ('file_pass','保管庫パスワード / File board password','FILEBOARDPASS {path}',[F_PATH],'manager',False),
    ('file_pass_clear','保管庫パスワード解除 / Remove file board password','FILEBOARDPASS {path} NONE CONFIRM',[F_PATH],'manager',True),
    ('file_owner','登録者を変更 / Change file owner','FILEOWNER {id} {user}',[F_ID,F_USER],'manager',True),
    ('connections','接続管理 / Connections','CONNECTIONS',[],'admin',False),
    ('disconnect','接続を切断 / Disconnect','DISCONNECT {id} CONFIRM',[F_ID],'admin',True),
    ('announce','一斉告知 / Announcement','ANNOUNCE {text}',[F_TEXT],'admin',True),
    ('shutdown','終了予告 / Schedule shutdown','SHUTDOWN {seconds} {text}',[('seconds','秒数 / Seconds','300'),F_TEXT],'admin',True),
    ('shutdown_cancel','終了予告を解除 / Cancel shutdown','SHUTDOWN CANCEL',[],'admin',True),
    ('shutdown_now','直ちに停止 / Shut down now','SHUTDOWN NOW CONFIRM',[],'admin',True),
    ('userlog','全接続履歴 / All sessions','USERLOG ALL',[],'admin',False),
    ('userstats','全利用集計 / All usage','USERSTATS ALL',[],'admin',False)]:
    add('admin.'+key,'admin',label,cmd,fields,role=role,confirm=confirm)
add('admin.board_info_edit','admin','長文ボード案内 / Edit board guide','INFOEDIT {id}',[F_ID],role='manager',edit=True)
add('admin.file_info_edit','admin','長文保管庫案内 / Edit file guide','FILEINFOEDIT {path}',[F_PATH],role='manager',edit=True)

for key,item in COMMANDS.items():
    if key.startswith('admin.file_'):item['role']='vault_manager'


def visible(item, state):
    feature={'mail':'mail','chat':'chat','files':'vault'}.get(item['group'])
    if feature and state.get('features',{}).get(feature)==0:return False
    role = item['role']
    return role == 'guest' or bool(state.get('user')) and (role == 'member' or state.get('admin') or role == 'manager' and state.get('sigop') or role=='vault_manager' and state.get('vault_sigop'))


def build_action(key, values, state, confirmed=False):
    if not isinstance(key,str) or not isinstance(values,dict):raise BridgeError('Invalid action')
    item = COMMANDS.get(key)
    if not item or not visible(item,state): raise BridgeError('This operation is not available for this account')
    if item['confirm'] and not confirmed: raise BridgeError('Confirmation required')
    args = {}
    for field in item['fields']:
        value = values.get(field['name'], field['default'])
        if not isinstance(value,str) or len(value.encode()) > 2048 or any(ord(c)<32 or ord(c)==127 for c in value):
            raise BridgeError('Invalid field value')
        if '|' in item['command'] and '|' in value: raise BridgeError('A field cannot contain |')
        args[field['name']] = value
    return item['command'].format(**args).rstrip(), item
