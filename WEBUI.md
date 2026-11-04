# Liner notes WebUI 0.72

初回導入は [詳細導入マニュアルのWebUI・Apache手順](INSTALL.md#webui-setup)を順に実施してください。設定全体、Host/Originの合わせ方、テンプレート権限、Forbidden等の切り分けをまとめています。この文書は構成・機能・開発用起動の補足です。

0.72のBBS本体と組み合わせる別プロセスのWebコネクタです。
0.71のruntimeには必要な同期機能がないため、本体とWebUIを揃えて更新します。

## FreeBSD portsでの導入

WEBUIオプションは既定ONです。aiohttp、専用ユーザー、rc.d、プログラム、
外部HTML/メッセージのsampleを導入します。自動起動はしません。
設定は `/usr/local/etc/liner-notes/web/web.toml` にあります。

- `[web] origin`: 実際の公開URLのオリジン（例 `https://bbs.example.org`）。
- `base_path`: `/notes/`。Apache側の公開パスも合わせます。
- `secure_cookies`: 公開時はtrue。
- `[bbs] host/port/telnet`: 実際のBBS待受（例127.0.0.1、23、true）。
- Web spoolは専用アカウントが書き込める非公開ディレクトリ。

```sh
sysrc liner_notes_web_enable=YES
service liner_notes start
service liner_notes status
service liner_notes_web start
service liner_notes_web status
```

既存のWebサービスがある場合は設定・独自テンプレートを保持します。旧web.tomlが
専用ユーザーから読めない場合はroot:liner_notes_web、0640にします。テンプレートの
ディレクトリは0755、公開本文は0644です。[更新手順](UPGRADING.md)も参照してください。
Apacheは `/usr/local/share/liner-notes/web/apache.conf.sample` と以下の説明を使います。
実際のTLS証明書は設置者が設定します。

## 構成

ブラウザー → 既存ApacheのHTTPS → Python/aiohttpコネクタ → localhostのBBS。
コネクタはDB・会員ファイル・保管庫を直接開きません。認証、会員レベル、
グループ、ボードパスワード、本人・SIGOP・管理者の権限判定はBBSに任せます。
メール・添付は送受信者限定で、管理者にも例外を追加しません。

BBS側にはオプトイン同期機能と、既存の転送処理を使う64KiBブロック機能を追加しています。
`CONNECTOR OPEN`で開始し、ランダムな要求番号付きの`CONNECTOR SYNC`で
応答の終端と状態を得ます。本文は`CONNECTOR LITERAL`で送り、`.`や
`/CANCEL`を編集命令として誤実行しません。トークンは認証情報ではなく、
通常接続と同じ認証・権限検査を通ります。既存Telnet接続は従来どおりです。
大きなブロックは`CONNECTOR BLOCK/DATA`で分割して渡し、通常入力の4096バイト上限や
従来の`FILEPUT`・`FILEGET`の上限を変更しません。会員・記事・ファイルの権限判定を
Web側へ移すものではありません。

ブラウザーごとにBBS接続を1本持ちます。補助端末は追加で1本使用し、
端末内で別途ログインします。パスワードの保存・再利用はしません。
端末を離れるとその接続を閉じ、Web側の接続は継続します。
ゲームは既存GAMES/DOORとPTY経由で起動します。OSのシェルをWebへ公開しません。

## 画面

| 分野 | Web側の入口 |
| --- | --- |
| 掲示板 | ボード一覧、記事、返信、投稿、下書き、検索、取り消し、題名変更、共通バッファ、巡回 |
| メール | 受送信一覧、本文、返信、検索、下書き、複数添付、SYSOP宛てメール |
| 保管庫 | 一覧、ファイル選択、登録・取得、再開、検索・新着、説明編集・削除、ボード管理 |
| 会話 | チャット、接続中の通知、個別トーク、拒否設定、接続者、Gather |
| 会員 | プロフィール閲覧・検索・編集・公開範囲、接続履歴、個人設定 |
| 管理 | 会員申請、権限、ボード設定、記事管理、保管庫設定、履歴、告知、切断、停止 |
| 補助端末 | 既存メニュー、端末固有の操作、FreeBSD外部ゲーム |

細かい操作は各画面の「その他の操作」にまとめています。
現段階では、一部の検索結果・管理帳票・案内はBBSの文字出力をWeb内に表示します。
入力フォームは`webui/commands.py`で定義し、表示条件とBBSの実際の拒否を併用します。
掲示板SIGOPは現在ボードの判定、保管庫SIGOPは閲覧対象と親ボードの公開済み担当情報で管理メニューを表示します。実行可否は毎回BBSが検査します。
バックアップ・復元・OS管理は従来の外部管理ツールを使います。

画面上部にログインを表示し、会員向け操作は入力画面を開く前にログインを案内します。
ゲスト投稿を許すボードでは、案内から明示的にゲスト投稿を選べます。
ヘッダの「投稿先を選ぶ」でボードを選択し、返信は元記事のボードに固定します。
記事・ボードのパンくずから親ボードへ戻れます。利用可能なゲームがない場合は
ゲームのナビゲーションを表示しません。
メールの添付は作成画面でファイルを選び、送信・下書き保存時にアップロードします。
添付番号の手入力は不要で、既存の下書き添付は名前付きで表示します。
プロフィール編集は全13項目を同時に表示し、項目ごとの変更可否はBBSで検査します。
チャットは左詰めとし、補助操作の結果を会話欄の上に表示します。

## HTMLテンプレート

`webui/templates/index.html`は画面の枠、`components.html`はログイン・投稿・
添付・保管庫登録・プロフィール編集フォームです。`index.html`の`{{components}}`へ
フォーム群を挿入します。投稿本文などは従来どおり文字として表示し、テンプレートとして
評価しません。動的な記事一覧等はJavaScriptが生成します。

インストール先は`/usr/local/etc/liner-notes/web/templates/`です。
`web.toml`の`[web] templates`で変更でき、相対パスは設定ファイルのディレクトリ基準です。
未指定・ファイル不在の場合は同梱テンプレートを使用します。
HTMLは次のページ取得で読み直します。JavaScriptが参照する`id`・`data-*`・フォームの
`name`は保持してください。外部メッセージの文言だけを変える場合はJSONを使います。

## 外部メッセージ

`webui/messages/ja.json`、`en.json`がWeb専用の外部ファイルです。
`name / tagline / banner / welcome / news / system / goodbye`を差し替えられます。
`greetings`は時間帯の開始時刻0〜23をキーにします。判定時刻は
`web.timezone`、既定はAsia/Tokyoです。`{{handle}} / {{date}} / {{time}}`を使用可能。
HTMLは実行せず文字として表示します。ファイルは次回の画面取得時に読み直します。
Telnet用のbanner.txtや挨拶ファイルには影響しません。
`birthday`は予約項目で、Web側への誕生日通知はまだ接続していません。

## 検証用起動

Python 3.11以上が必要です。FreeBSDでは3.12で検証しています。

```sh
make
python3.12 -m venv .web-venv
.web-venv/bin/pip install -r webui/requirements.txt
cp webui/example.toml webui/local.toml
```

`local.toml`のBBS接続先をテスト用runtimeのポートに変更します。
HTTPだけのlocalhost検証では`origin="http://127.0.0.1:8088"`、
`secure_cookies=false`にし、spoolをテスト用の絶対パスへ変更してください。
公開運用ではHTTPSのoriginと`secure_cookies=true`を使います。

```sh
.web-venv/bin/python -m webui --config webui/local.toml
# ブラウザー: http://127.0.0.1:8088/notes/
```

通常画面ではCookie＋CSRFトークン、WebSocketでは同一Origin＋サブプロトコルの
CSRFトークンを検査します。ログイン成功時はセッションを交換します。
秘密入力と本文をアクセスログに記録しないでください。
コネクタ再起動時は接続を破棄し、利用者に再ログインしてもらいます。
結果が不明な更新操作を自動再送しません。

## portsを使用しない手動配置とrc.conf

以下はソースから個別配置する場合だけの手順です。portsで導入済みなら実行不要です。BBS本体の更新は別作業です。
依存を入れたPythonを`--python`へ指定してください。一般のPythonへ
依存を入れない場合、サービス専用venvをこの配置外に作って指定できます。

```sh
python3.12 webui/install.py --python /absolute/path/to/web-venv/bin/python
pw useradd liner_notes_web -d /nonexistent -s /usr/sbin/nologin
install -d -o liner_notes_web -g liner_notes_web -m 700 /var/db/liner-notes-web/spool
install -d -m 755 /var/log/liner-notes
chown root:liner_notes_web /usr/local/etc/liner-notes/web/web.toml
chmod 640 /usr/local/etc/liner-notes/web/web.toml
```

既存アカウントがある場合はuseraddを繰り返しません。
`web.toml`のorigin、base_path、BBSのポートを設置環境に合わせて編集します。
コードは`/usr/local/share/liner-notes/web/webui/`、起動プログラムは
`/usr/local/sbin/liner-notes-web`、設定・メッセージは
`/usr/local/etc/liner-notes/web/`、spoolは`/var/db/liner-notes-web/spool/`です。
BBSのデータをWebサービスアカウントへ読ませる必要はありません。

```sh
sysrc liner_notes_web_enable=YES
sysrc liner_notes_web_config=/usr/local/etc/liner-notes/web/web.toml
service liner_notes_web start
```

rc.dはrc.confの`liner_notes_web_program / config / user / pidfile / log`を読みます。
既定pidは`/var/run/liner-notes-web.pid`、ログは`/var/log/liner-notes/web.log`。
更新時はサービスを停止して配置し直します。設定とメッセージは既存ファイルを
上書きせず、新しい雛形を`.sample`に出力します。テンプレートも同様です。
独自テンプレートがある場合、新版の`.sample`に加わったフォーム・要素を比較して
反映してください。自動再起動・自動DB変更はしません。
インストーラーは`--destdir`による事前確認にも対応します。

`webui/deployment/apache.conf`を既存TLS VirtualHostへ組み込む例を用意しています。
Apache 2.4.47以上、mod_proxy/mod_proxy_httpを前提にし、既存の証明書設定を使います。
ProxyPreserveHostが必要です。`/notes/`は例で、別パスにも変更可能です。
Apache設定の反映と実TLS経由の運用試験は設置先で行ってください。

## 転送と現時点の制限

- ブラウザーからHTTPで受け、私有spoolでサイズ・SHA-256を確認してからBBSへ
  渡します。新しい本体では64KiBごとに確定応答を待ちます。旧Web対応本体では
  従来の小さいブロックへフォールバックします。1ファイル1GiB、画面の複数選択は16個／16GiBまで。
  設置先BBSのlimits.confが小さい場合はそちらで拒否されます。
- HTTP受信後は202を返して登録を継続し、画面から進捗を照会します。
  「HTTP受信済み」は「BBS登録完了」と別です。画面を閉じても受信済みの登録は
  継続しますが、サービス停止・BBS切断時の継続は保証しません。完了確認後に一覧へ反映します。
  最終照合の待ち時間は`[bbs] transfer_timeout`で設定します（既定120秒、30〜600秒）。
- 再開候補にファイル名・登録番号・サイズを表示し、選択して再開できます。
  この版より前の失敗分にはWeb側の記録がないため、自動列挙しません。
  HTTP側では元ファイルをもう一度選び、
  コネクタからBBSへは未受信位置から送ります。サイズ・ハッシュが異なる
  ファイルは拒否します。登録先・名前・説明は最初の登録内容を保持します。
  記録はspool配下の`resumes/`へ所有会員別に保存し、他会員へ公開しません。
  記録はBBSのアクセス権や確定位置の代わりには使いません。
- 完了済みの同じ登録番号を再試行しても重複登録しません。中断した登録は
  状態照会・破棄のフォームから管理できます。
- 取得は一時URLとCookieを使ってストリーミングします。JavaScriptで1GiB全体を
  メモリへ展開しません。SHA-256を確認後にHTTP応答を完結させます。
  履歴はWebコネクタからHTTPへの送出完了を記録し、利用者のディスクへの
  保存完了を保証するものではありません。ブラウザーの取得一覧も確認してください。
- 一括取得は個別のダウンロードです。ブラウザーによって複数ダウンロードの
  許可が必要です。選択を自動で「完了済み」に変えません。
- 64KiB転送でもコネクタとBBS間の待ち合わせ・ディスク確定は残ります。
  1GiB／16GiBの所要時間・同時大量転送は未検証です。
- Web端末内のZMODEMは実装していません。Webのファイル転送画面を使用します。
- 再読み込み前の未送信文章はブラウザーのメモリ内だけです。継続する場合は
  BBS下書きへ保存します。複数タブで同じ接続の対話を同時操作しないでください。
- 匿名接続もBBSの接続枠を使用します。max_sessionsと無操作期限を設定します。
  BBSには接続元がlocalhostとして見えるため、公開前にApache側の接続制限・
  レート制限を設置方針に合わせて設定する必要があります。

## 開発時の検証

```sh
make test-web PYTHON=.web-venv/bin/python
.web-venv/bin/python tests/test_web_service.py
# 任意: 実ファイルを実行せず転送・SHA-256照合だけに使用
.web-venv/bin/python tests/test_web_transfers.py ./runtime ./vaultctl /path/to/fixture
```

実BBSへのTelnet接続、認証、本文の命令誤認防止、4096バイト行、記事・メール、
CSRF/Origin、私信・添付の会員間分離、公開ファイル、SHA-256、再開・重複防止を
隔離データで検証します。実ブラウザー試験は次の専用フィクスチャを使用します。
固定のテスト会員を作るため、公開サービスに接続させないでください。

```sh
.web-venv/bin/python tests/web_browser_fixture.py ./runtime ./vaultctl 18889
# 別端末。Playwrightとブラウザーをインストールした環境で実行
node tests/test_web_browser.cjs http://127.0.0.1:18889/notes/ /tmp/web-browser-results
```

`LN_BROWSER_CHANNEL=msedge`等でブラウザーを選べます。既定はPlaywright Chromium。
実BBSで投稿・添付送受信・プロフィール保存・チャット・端末表示を確認します。
再開候補の画面選択のみ制御したHTTP応答を使用し、実際の再開・所有者検査は
`test_web_transfers.py`で別途確認します。今回の結果は
[WEBUI_REVIEW_VERIFICATION.md](WEBUI_REVIEW_VERIFICATION.md)を参照してください。

2026-11-01、FreeBSD 15.1/amd64で上記試験と配置テストを実行しました。
追加確認: 256KiB下書きの保存・再開、保管庫の親SIGOP権限と担当外での拒否、
一度だけ使用できるダウンロードURL、ブラウザー端末内のログイン・PTYゲーム・BBS復帰。
既存のadapter/Telnet/メール/チャット回帰試験も通過しました。
実ブラウザーでは記事閲覧・返信、ファイルの登録・取得（元データとSHA-256一致）、
モバイルナビゲーション、管理メニュー、端末と外部ゲームを確認しました。
テスト用の隔離データを使用し、本番データは使っていません。
rc.dについてもFreeBSDの実際のrc.subr/daemonで起動・HTTP応答・状態確認・二重起動拒否・
再起動・停止・設定エラーによる起動失敗を確認しました。root起動から非rootユーザーへの
権限降下を含みます。`liner_notes_web_program`と`liner_notes_web_user`はrc.subrの予約名と
衝突するため、読み込み後に内部変数へ移して明示的にdaemonへ渡します。
旧スクリプトの`unrecognized arguments: -P ...`はrc.dファイルの差し替えで修正できます。
PythonやBBS本体を再ビルドする必要はありません。

端末表示は同梱のxterm.js 6.0.0＋addon-fit 0.11.0（MIT）です。
配布元のsha512 integrityを確認して取得し、LICENSEをstatic/へ同梱しました。
実行時に外部CDNへアクセスしません。xtermの表示計算にインラインCSSを許可し、
スクリプトは同一サイトのみ、投稿などの表示はtextContentに限定します。

## 継続する改善・検証

- 管理帳票・検索結果の専用画面化と、全操作のブラウザー受入試験。
- 1GiB以上の一括運用・同時接続の負荷試験、転送のさらなる高速化。
- 設置環境ごとのApache/HTTPS経由の大容量転送確認。

0.72には現在の機能を収録しています。上記の性能・全操作の受入確認まで完了したという意味ではありません。
公式資料: [ApacheのWebSocket転送](https://httpd.apache.org/docs/2.4/mod/mod_proxy_wstunnel.html)、
[xterm.jsの組み込み上の注意](https://xtermjs.org/docs/guides/security/)。
