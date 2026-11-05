# 名前付きコマンド・Webフォーム索引

この索引は名前付き操作とWebの対応フォームです。画面・対象による権限は本体が再検査します。member等の分類はフォームの入口条件であり、対象の記事・所有者・SIGOP範囲への許可を保証しません。`{...}` は入力欄で、波括弧を実際に入力しません。短縮キーは[メニュー](navigation.md)と各機能章を参照してください。

## 補助コマンドと画面による違い

| 操作 | 用途 |
|---|---|
| `POST 題名` / `REPLY 記事番号 題名` | 新規投稿 / 指定記事への返信 |
| `RESPOND 題名` | 現在読んでいる記事への返信 |
| `PROFILEID 会員番号` | 会員番号でプロフィールを表示 |
| `FILECATEGORIES` | 保管庫のカテゴリ一覧。保管庫メニューのCに相当 |
| `MAILFILES` / `MAIL FILES` | 自分の非公開添付一覧 |
| `UIN パス\|説明` | 保管庫メニューでの `ZUPLOADIN` の短縮形 |
| `WHO` | 接続人数。接続者の詳細一覧は `ONLINE` |
| `PING` | 疎通確認。`PONG` 応答 |

一覧の続きを表示する `SEARCHFROM`、`FINDFROM`、`NEWSFROM`、`PROFILESFROM`、`FILESEARCHFROM`、`FILESINFROM`、`FILENEWSFROM` は、開始位置と元の条件を指定するコネクタ向け操作です。通常のメニューでは `+` が条件を保持して次ページを取得します。メール内の `SEARCHFROM` / `NEWSFROM` はメールを対象にするため、主画面の記事検索と混同しません。

管理者の案内は `HELP ADMINMEMBERS`、`HELP ADMINBOARDS`、`HELP ADMINFILES`、`HELP ADMINCONNECTIONS`、`HELP ADMINSTORAGE` から用途別に読めます。記事管理は `HELP CONTENT`。案内の閲覧と実行のどちらにも権限確認があります。

以下はWebのフォームから送る名前付き操作の索引です。

## board

| 操作 | 名前付きコマンド | 入力・入口条件 |
|---|---|---|
| ボード案内 | `INFO` |  / guest |
| 返信ツリー | `TREE {id}` | 番号 / ID / guest |
| 未読記事 | `UNREAD` |  / member |
| 次の未読を読む | `NEW` |  / member |
| 次の未読ボード | `NEXTBOARD` |  / member |
| 未読のあるスレッド | `NOTES UNREAD` |  / member |
| 検索 | `FIND {scope} * {query}` | 範囲 / Scope; 検索語 / Search / member |
| 投稿者で検索 | `FIND {scope} {author} *` | ログイン名 / Login; 範囲 / Scope / member |
| 日付で検索 | `NEWS {date}` | 日付 YYYY-MM-DD / Date / member |
| 新着巡回 | `CHECK ARTICLES {scope}` | HERE / SUBTREE / ALL / member |
| 巡回の続き | `CHECK NEXT` |  / member |
| 確認日時を保存 | `CHECK DONE` |  / member |
| 巡回を中止 | `CHECK CANCEL` |  / member |
| 既読にする | `SEEN {id}` | 番号 / ID / member |
| 未読に戻す | `UNSEEN {id}` | 番号 / ID / member |
| 共通バッファへ | `GET {id}` | 番号 / ID / member |
| 題名を変更 | `RETITLE {id} {text}` | 番号 / ID; 内容 / Text / member |
| 記事を取り消す | `WITHDRAW {id} CONFIRM` | 番号 / ID / member / 確認あり |
| 保存した下書き | `DRAFT` |  / member |
| 下書きを再開 | `RESUME` |  / member / エディタ |
| 下書きを削除 | `DRAFT DELETE {revision}` | 版番号 / Revision / member / 確認あり |
| ボードのパスワード | `BOARD {id}` | 番号 / ID / guest |
| 前の記事 | `PREV` |  / guest |
| 次の記事 | `NEXT` |  / guest |
| 親記事 | `PARENT` |  / guest |
| 返信記事 | `CHILD` |  / guest |
| 連続読み出し | `SCAN {order} {scope} {author} {query}` | ASC / DESC; HERE / SUBTREE / ALL; 投稿者または* / Author or *; 検索語または* / Query or * / member |

## mail

| 操作 | 名前付きコマンド | 入力・入口条件 |
|---|---|---|
| 受信 | `MAIL INBOX` |  / member |
| 送信 | `MAIL SENT` |  / member |
| 未読 | `MAIL UNREAD` |  / member |
| 下書き | `MAIL DRAFT` |  / member |
| 添付一覧 | `MAIL FILES` |  / member |
| 本文検索 | `MAIL SEARCH {query}` | 検索語 / Search / member |
| 日付で検索 | `MAIL NEWS {date}` | 日付 YYYY-MM-DD / Date / member |
| 新着巡回 | `CHECK MAIL` |  / member |
| 共通バッファへ | `MAIL GET {id}` | 番号 / ID / member |
| 下書きを再開 | `MAIL RESUME` |  / member / エディタ |
| 下書きを削除 | `MAIL DRAFT DELETE {revision}` | 版番号 / Revision / member / 確認あり |
| メールを削除 | `MAIL DELETE {id}` | 番号 / ID / member / 確認あり |
| 未使用添付を回収 | `MAIL DROP {id} CONFIRM` | 番号 / ID / member / 確認あり |
| 管理者へメール | `SYSOPMAIL {title}` | 題名 / Subject / guest / エディタ |
| 添付の登録状態 | `MFILESTATUS {id}` | 番号 / ID / member |
| 中断した添付を破棄 | `MFILECANCEL {id}` | 番号 / ID / member / 確認あり |
| 連続読み出し | `MAIL SCAN {order}` | ASC / DESC / member |
| 前へ | `MAIL P` |  / member |
| 次の未読 | `MAIL N` |  / member |
| 再表示 | `MAIL L` |  / member |

## settings

| 操作 | 名前付きコマンド | 入力・入口条件 |
|---|---|---|
| 新着通知 | `MAIL NOTIFY {value}` | ON / OFF / member |
| 会員情報 | `ACCOUNT` |  / member |
| 巡回日時の更新 | `CHECKMODE {value}` | AUTO / MANUAL / member |
| 新着通知 | `NOTIFY {value}` | ON / OFF / member |
| 共通バッファを消去 | `BUFFER CLEAR` |  / member / 確認あり |
| 退会する | `RETIRE CONFIRM` |  / member / 確認あり |
| 保存した設定 | `PREFS` |  / member |
| 端末の言語 | `LANG {value}` | ja / en / member |
| 端末のページ停止 | `PAGER {value}` | ON / OFF / member |
| 端末サイズ | `PAGER SIZE {columns} {rows}` | 列 / Columns; 行 / Rows / member |
| 端末エディタ | `EDITOR {value}` | BETA / LINE / member |
| 端末の案内量 | `MENUMODE {value}` | BEGINNER / STANDARD / EXPERT / member |
| 端末転送方式 | `PROTOCOL {value}` | 方式 / Protocol / member |
| 設定を保存 | `PREFS SAVE` |  / member |
| 設定を再読込 | `PREFS LOAD` |  / member |
| パスワードを変更 | `PASSWD` |  / member |
| 共通編集バッファ | `BUFFER` |  / member / エディタ |

## files

| 操作 | 名前付きコマンド | 入力・入口条件 |
|---|---|---|
| ファイルボード | `FILEBOARDS` |  / member |
| ファイル一覧 | `FILES` |  / member |
| 取得回数 | `FILESTATS` |  / member |
| 取得履歴 | `FILEHISTORY` |  / member |
| 変更履歴 | `FILEAUDIT` |  / member |
| ボード案内 | `FILEBOARDINFO {path}` | ファイルボード / File board / member |
| ボードを選ぶ | `FILEBOARD {path}` | ファイルボード / File board / member |
| 子ボード | `FILECHILDREN {path}` | ファイルボード / File board / member |
| 配下の一覧 | `FILESIN {path}` | ファイルボード / File board / member |
| 名前で検索 | `FILESEARCH {query}` | 検索語 / Search / member |
| ファイル詳細 | `FILE {id}` | 番号 / ID / member |
| テキスト表示 | `FILETYPE {id}` | 番号 / ID / member |
| 説明を編集 | `FILEDESC {id} {text}` | 番号 / ID; 内容 / Text / member |
| ファイル名を変更 | `FILERENAME {id} {text}` | 番号 / ID; 内容 / Text / member |
| 分類を変更 | `FILECATEGORY {id} {path}` | 番号 / ID; ファイルボード / File board / member |
| テキスト属性 | `FILEASCII {id} {value}` | 番号 / ID; ON / OFF / member |
| ボードへコピー | `FILECOPY {id} {path}` | 番号 / ID; ファイルボード / File board / member |
| 新着巡回 | `CHECK FILES {path}` | ファイルボード / File board / member |
| 一括選択へ追加 | `BATCH ADD {id}` | 番号 / ID / member |
| 選択を解除 | `BATCH REMOVE {id}` | 番号 / ID / member |
| ワイルドカード選択 | `BATCH MATCH {text}` | パターン / Pattern / member |
| 選択一覧 | `BATCH LIST` |  / member |
| 選択をクリア | `BATCH CLEAR` |  / member |
| ファイルを削除 | `FILEDELETE {id} CONFIRM` | 番号 / ID / member / 確認あり |
| 非公開にする | `FILEHIDE {id} CONFIRM` | 番号 / ID / member / 確認あり |
| 再公開 | `FILERESTORE {id} CONFIRM` | 番号 / ID / member / 確認あり |
| 保管庫のパスワード | `FILEUNLOCK {path}` | ファイルボード / File board / member |
| 配下の新着 | `FILENEWS {path}\|{date}` | ファイルボード / File board; YYYY-MM-DD / member |
| 配下を検索 | `FILESEARCH {path}\|{query}` | ファイルボード / File board; 検索語 / Search / member |
| 中断した登録の状態 | `FILESTATUS {id}` | 番号 / ID / member |
| 中断した登録を破棄 | `FILECANCEL {id}` | 番号 / ID / member / 確認あり |
| 選択ファイルをコピー | `BATCH COPY {path}` | ファイルボード / File board / vault_manager / 確認あり |

## people

| 操作 | 名前付きコマンド | 入力・入口条件 |
|---|---|---|
| 自分のプロフィール | `PROFILE` |  / member |
| 会員一覧 | `PROFILES` |  / member |
| プロフィール検索 | `PROFILES {query}` | 検索語 / Search / member |
| 項目で検索 | `PROFILEFIND {field} {query}` | 項目 / Field; 検索語 / Search / member |
| 所属グループで検索 | `PROFILEFIND GROUP {group}` | グループ番号 / Group ID / member |
| プロフィールを見る | `PROFILE {login}` | ログイン名 / Login / member |
| 項目を編集 | `PROFILESET {field} {text}` | 項目 / Field; 内容 / Text / member |
| 公開範囲 | `PROFILEVIS {field} {value}` | 項目 / Field; PRIVATE / MEMBERS / MONTHDAY / member |
| 長文自己紹介 | `BIOEDIT` |  / member / エディタ |
| ハンドル | `HANDLE {text}` | 内容 / Text / guest |
| 接続者 | `ONLINE` |  / member |
| 接続者メモ | `MEMO {text}` | 内容 / Text / member |
| SYSOP呼び出し | `CALL` |  / guest |
| 接続履歴 | `USERLOG` |  / member |
| 利用時間 | `USERSTATS` |  / member |
| 接続履歴の表 | `USERTABLE` |  / member |

## chat

| 操作 | 名前付きコマンド | 入力・入口条件 |
|---|---|---|
| 個別トーク | `TALK {id}` | 接続番号 / Connection ID / member |
| チャットへ入る | `CHAT` |  / member |
| チャットを出る | `/LEAVE` |  / member |
| 接続者 | `W` |  / member |
| 状態 | `#` |  / member |
| メモ | ```` |  / member |
| SYSOP呼び出し | `D` |  / member |
| チャットから切断 | `/KICK {login}` | ログイン名 / Login / admin / 確認あり |
| トーク受信 | `/TALK {value}` | ON / OFF / member |
| 受信拒否 | `/MUTE {value}` | ログイン名 / Login / member |
| 受信拒否を解除 | `/UNMUTE {value}` | ログイン名 / Login / member |
| 受信設定を確認 | `/MUTES` |  / member |

## gather

| 操作 | 名前付きコマンド | 入力・入口条件 |
|---|---|---|
| 伝言を読む・書く | `GATHER` |  / member |
| 伝言を読む | `GATHER READ` |  / member |

## help

| 操作 | 名前付きコマンド | 入力・入口条件 |
|---|---|---|
| 結果の続き | `+` |  / guest |
| 対話を中止 | `/CANCEL` |  / guest |
| 操作ヘルプ | `HELP {topic}` | 項目 / Topic / guest |

## games

| 操作 | 名前付きコマンド | 入力・入口条件 |
|---|---|---|
| ゲーム一覧 | `GAMES` |  / member |

## admin

| 操作 | 名前付きコマンド | 入力・入口条件 |
|---|---|---|
| 会員一覧 | `MEMBERS` |  / admin |
| 会員詳細 | `MEMBER {user}` | 会員番号 / Member ID / admin |
| 権限 | `RIGHTS {user}` | 会員番号 / Member ID / manager |
| 入会申請 | `APPLICATIONS` |  / admin |
| 申請内容 | `APPLICATION {user}` | 会員番号 / Member ID / admin |
| 申請を修正 | `APPLICATIONSET {user} {field} {text}` | 会員番号 / Member ID; 項目 / Field; 内容 / Text / admin |
| 入会を承認 | `APPROVE {user}` | 会員番号 / Member ID / admin / 確認あり |
| 申請を却下 | `REJECT {user} CONFIRM` | 会員番号 / Member ID / admin / 確認あり |
| 申請を削除 | `DELETEAPPLICATION {user} CONFIRM` | 会員番号 / Member ID / admin / 確認あり |
| 利用停止 | `SUSPEND {user}` | 会員番号 / Member ID / admin / 確認あり |
| 停止解除 | `RESUME {user}` | 会員番号 / Member ID / admin / 確認あり |
| 会員を削除 | `DELETEUSER {user} CONFIRM` | 会員番号 / Member ID / admin / 確認あり |
| パスワード再設定 | `RESETPASS {user}` | 会員番号 / Member ID / admin / 確認あり |
| 会員レベル | `LEVEL {user} {level}` | 会員番号 / Member ID; レベル 0–5 / Level / admin / 確認あり |
| グループへ追加 | `GROUP {user} {group}` | 会員番号 / Member ID; グループ 1–100 / Group / manager |
| グループから除外 | `UNGROUP {user} {group}` | 会員番号 / Member ID; グループ / Group / manager / 確認あり |
| 所属を移行 | `GROUPMOVE {source} {target} CONFIRM` | 元 / From; 先 / To / admin / 確認あり |
| 会員プロフィール編集 | `MEMBERPROFILE {user} {field} {text}` | 会員番号 / Member ID; 項目 / Field; 内容 / Text / admin |
| ボード設定 | `BOARDINFO {id}` | 番号 / ID / manager |
| ボードを作成・変更 | `BOARDSET {id}\|{parent}\|{read}\|{write}\|{title}\|{description}` | 番号 / ID; 親 / Parent; 閲覧条件 / Read; 投稿条件 / Write; 題名 / Title; 説明 / Description / manager |
| ボードの属性・上限 | `BOARDOPTIONS {id} {kind} {order} {count} {bytes} {keep}` | 番号 / ID; BOARD / MENU; 配置 / Order; 記事数 / Articles; 本文容量 / Bytes; ログ保持 / Keep logs / manager |
| ボードパスワード | `BOARDPASS {id}` | 番号 / ID / manager |
| パスワードを解除 | `BOARDPASS {id} NONE CONFIRM` | 番号 / ID / manager / 確認あり |
| ゲスト投稿 | `BOARDGUEST {id} {value}` | 番号 / ID; ON / OFF / manager |
| 担当ボード | `SIGOPS` |  / manager |
| 担当者を変更 | `SIGOP {id} {user}` | 番号 / ID; 会員番号またはNONE / Member ID or NONE / manager / 確認あり |
| 担当グループ | `SIGOPGROUP {id} {group}` | 番号 / ID; グループ / Group / admin / 確認あり |
| ボードと所属を移行 | `BOARDGROUP {id} {source} {target} CONFIRM` | 番号 / ID; 元グループ / From; 先グループ / To / admin / 確認あり |
| 記事投稿者を変更 | `AUTHOR {id} {user} CONFIRM` | 番号 / ID; 会員番号 / Member ID / manager / 確認あり |
| 記事を非表示 | `HIDE {id}` | 番号 / ID / manager / 確認あり |
| 記事を再表示 | `RESTORE {id}` | 番号 / ID / manager / 確認あり |
| 非表示記事を確認 | `INSPECT {id}` | 番号 / ID / manager |
| スレッドを移動 | `MOVE {id} {target} CONFIRM` | 番号 / ID; 移動先ボード / Destination / admin / 確認あり |
| 枝を移動 | `MOVEBRANCH {id} {target} CONFIRM` | 番号 / ID; 移動先ボード / Destination / admin / 確認あり |
| 空ボードを削除 | `BOARDDELETE {id} CONFIRM` | 番号 / ID / admin / 確認あり |
| ボード配下を削除 | `BOARDDELETEALL {id} CASCADE CONFIRM` | 番号 / ID / admin / 確認あり |
| ボード変更履歴 | `BOARDLOG` |  / manager |
| 閲覧・投稿履歴 | `BOARDACCESS {id}` | 番号 / ID / manager |
| 操作履歴 | `AUDIT` |  / manager |
| 記事別履歴 | `AUDIT ARTICLE {id}` | 番号 / ID / manager |
| 操作者別履歴 | `AUDIT ACTOR {login}` | ログイン名 / Login / manager |
| ファイルボード権限 | `FILEBOARDSET {path}\|{read}\|{write}` | ファイルボード / File board; 閲覧条件 / Read; 登録条件 / Upload / vault_manager |
| ボード表示名 | `FILEBOARDNAME {path}\|{text}` | ファイルボード / File board; 内容 / Text / vault_manager |
| ファイルボード属性 | `FILEBOARDOPTIONS {path}\|{kind} {order} {keep}` | ファイルボード / File board; BOARD / MENU; 配置 / Order; 履歴保持 / History / vault_manager |
| 保管庫SIGOP | `FILEBOARDSIGOP {path}\|{user}` | ファイルボード / File board; 会員番号またはNONE / Member ID or NONE / vault_manager / 確認あり |
| 保管庫パスワード | `FILEBOARDPASS {path}` | ファイルボード / File board / vault_manager |
| 保管庫パスワード解除 | `FILEBOARDPASS {path} NONE CONFIRM` | ファイルボード / File board / vault_manager / 確認あり |
| 登録者を変更 | `FILEOWNER {id} {user}` | 番号 / ID; 会員番号 / Member ID / vault_manager / 確認あり |
| 接続管理 | `CONNECTIONS` |  / admin |
| 接続を切断 | `DISCONNECT {id} CONFIRM` | 番号 / ID / admin / 確認あり |
| 一斉告知 | `ANNOUNCE {text}` | 内容 / Text / admin / 確認あり |
| 終了予告 | `SHUTDOWN {seconds} {text}` | 秒数 / Seconds; 内容 / Text / admin / 確認あり |
| 終了予告を解除 | `SHUTDOWN CANCEL` |  / admin / 確認あり |
| 直ちに停止 | `SHUTDOWN NOW CONFIRM` |  / admin / 確認あり |
| 全接続履歴 | `USERLOG ALL` |  / admin |
| 全利用集計 | `USERSTATS ALL` |  / admin |
| 長文ボード案内 | `INFOEDIT {id}` | 番号 / ID / manager / エディタ |
| 長文保管庫案内 | `FILEINFOEDIT {path}` | ファイルボード / File board / vault_manager / エディタ |
