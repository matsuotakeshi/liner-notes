# WebUIとApacheの構成・差し替え

<a id="webui-setup"></a>

## WebUIとApache HTTPS

### 接続経路と準備

```text
ブラウザー → https://bbs.example.org/notes/
           → Apache → http://127.0.0.1:8088/notes/
                    → BBS 127.0.0.1:2222（23運用なら23）
```

WebコネクタとBBSは別サービスです。WebはDBを直接開かず、通常の会員認証・権限判定をBBSへ依頼します。まずTelnetでログインできることを確認します。BBS本体とWebUIは0.74に揃えます。

```sh
service liner_notes status
pkg info liner-notes
ls -l /usr/local/sbin/liner-notes-web /usr/local/etc/rc.d/liner_notes_web
id liner_notes_web
ls -ld /var/db/liner-notes-web/spool
```

Web実行ファイルがなければ、WEBUIオプションが無効です。既存サービスを停止・バックアップしてから [更新手順](upgrade.md)でWEBUIを有効にしたpkgへ入れ替えます。ports導入済み環境に手動インストーラーを重ねる必要はありません。

### web.tomlを編集する

```sh
ee /usr/local/etc/liner-notes/web/web.toml
```

例の全体です。すでに設定がある場合は対応する項目を編集し、同じセクションを重複して追加しません。

```toml
[web]
listen = "127.0.0.1"
port = 8088
origin = "https://bbs.example.org"
base_path = "/notes/"
secure_cookies = true
max_sessions = 32
browser_idle_seconds = 1800
messages = "/usr/local/etc/liner-notes/web/messages"
templates = "/usr/local/etc/liner-notes/web/templates"
timezone = "Asia/Tokyo"
spool = "/var/db/liner-notes-web/spool"
max_upload_bytes = 1073741824
max_spool_bytes = 17179869184

[bbs]
host = "127.0.0.1"
port = 2222
telnet = true
timeout = 30
transfer_timeout = 120
```

変更する箇所:

| 項目 | 合わせる値 |
| --- | --- |
| `origin` | ブラウザーの公開元。`https://bbs.linernotes.site`など。`/notes/`を含めない |
| `base_path` | `/notes/`。Apacheの転送元・転送先とも一致させる |
| `[bbs] port` | 実際のruntime待受。23運用なら23。8088ではない |
| `secure_cookies` | HTTPS公開ではtrue。ブラウザーから平文HTTPでログインする設定ではない |
| `max_sessions` | Webの接続上限。上の32は接続枠に余裕を持たせた運用例、配布既定は64 |
| `transfer_timeout` | BBSへの最終登録・照合待ち。既定120秒、許容30〜600秒 |

Web利用者は匿名でもBBS接続を1本使います。補助端末・ゲーム画面ではさらに1本使うため、BBSの `connections` をすべてWebへ割り当てず、Telnet利用分も残します。

配置権限を確認します。標準アカウントの場合の設定です。

```sh
chown root:liner_notes_web /usr/local/etc/liner-notes/web/web.toml
chmod 640 /usr/local/etc/liner-notes/web/web.toml
chmod 755 /usr/local/etc/liner-notes/web \
  /usr/local/etc/liner-notes/web/templates /usr/local/etc/liner-notes/web/messages
chmod 644 /usr/local/etc/liner-notes/web/templates/*.html \
  /usr/local/etc/liner-notes/web/messages/*.json
su -m liner_notes_web -c '/bin/sh -s' <<'SH'
set -eu
test -r /usr/local/etc/liner-notes/web/web.toml
test -r /usr/local/etc/liner-notes/web/templates/index.html
test -r /usr/local/etc/liner-notes/web/templates/components.html
test -w /var/db/liner-notes-web/spool
echo 'Web permissions OK'
SH
```

`Web permissions OK`が出てから起動します。spoolは `liner_notes_web:liner_notes_web`・0700です。WebユーザーをBBSのグループへ追加したり、`/var/db/liner-notes`を公開したりする必要はありません。

```sh
sysrc liner_notes_web_enable=YES
sysrc liner_notes_web_config=/usr/local/etc/liner-notes/web/web.toml
service liner_notes_web start
service liner_notes_web status
sockstat -4l -P tcp -p 8088
tail -n 50 /var/log/liner-notes/web.log
```

既存レビュー版から切り替える場合は、配置確認後に `sysrc liner_notes_web_program=/usr/local/sbin/liner-notes-web` も指定します。すでに起動中なら `start` の代わりに `restart` を使います。

### Apacheより手前で動作を確認する

curlがなければ `pkg install curl` で導入します。`Host`は実際の公開名へ置き換えます。

```sh
curl -i -H 'Host: bbs.example.org' http://127.0.0.1:8088/notes/
```

正常なら `200 OK` とHTMLです。これは疎通確認であり、ブラウザーのログイン確認はHTTPS側で行います。`Host`を省くと `Unexpected Host` の403になります。`[web] origin`が `https://bbs.example.org` なら `Host`に8088を付けません。

### ApacheのVirtualHost

既存のApache設定をバックアップし、実際に読み込まれるVirtualHostファイルを編集します。

```sh
/usr/local/sbin/httpd -v
/usr/local/sbin/httpd -M
/usr/local/sbin/apachectl -S
```

`proxy_module`、`proxy_http_module`、`ssl_module`を確認します。足りなければApacheのモジュール設定で有効化します。以下は**このBBS専用ホスト名**の例で、既存のVirtualHostへ必要な項目を統合します。すでに80/443のListenがあれば重複して追加しません。`SSLCertificateFile`と`SSLCertificateKeyFile`には、取得済みのその公開名用証明書を指定します。証明書の発行・更新は既存のACME等の運用を使います。

```apache
<VirtualHost *:80>
    ServerName bbs.example.org
    Redirect permanent / https://bbs.example.org/
</VirtualHost>

<VirtualHost *:443>
    ServerName bbs.example.org
    SSLEngine on
    SSLCertificateFile "/usr/local/etc/letsencrypt/live/bbs.example.org/fullchain.pem"
    SSLCertificateKeyFile "/usr/local/etc/letsencrypt/live/bbs.example.org/privkey.pem"

    ProxyRequests Off
    ProxyPreserveHost On
    RedirectMatch 302 ^/$ /notes/
    RedirectMatch 308 ^/notes$ /notes/
    ProxyPass        /notes/ http://127.0.0.1:8088/notes/ upgrade=websocket timeout=600
    ProxyPassReverse /notes/ http://127.0.0.1:8088/notes/

    <Location /notes/>
        Require all granted
        LimitRequestBody 1073741824
    </Location>

    ErrorLog "/var/log/httpd-bbs-error.log"
    CustomLog "/var/log/httpd-bbs-access.log" combined
</VirtualHost>
```

`base_path=/notes/`なら転送先も `/notes/` です。転送先を `/` に変えて解決しようとしないでください。専用ホストの `/` は上の302で `/notes/`へ案内するため、トップページがDocumentRootのForbiddenになる問題も避けられます。既存サイトと共存させる場合は `/` のリダイレクトを省略します。

HTTPSのVirtualHost内に「同じHTTPS URLへ常時転送するRewriteRule」を入れないでください。80側だけでHTTPSへ転送します。WebSocketの `upgrade=websocket` はApache 2.4.47以降のHTTPプロキシを使います。[Apache公式のWebSocket転送](https://httpd.apache.org/docs/2.4/mod/mod_proxy_wstunnel.html)を参照してください。

```sh
/usr/local/sbin/apachectl configtest
```

`Syntax OK`を確認してから反映します。既存のApacheを使っている場合:

```sh
service apache24 reload
curl -I https://bbs.example.org/
curl -I https://bbs.example.org/notes/
```

トップは `/notes/`への転送、`/notes/`は200が目安です。新規Apacheでは設定後に `sysrc apache24_enable=YES`、`service apache24 start` とします。証明書エラーを `curl -k` で隠して公開完了としないでください。

### ブラウザーでの確認と差し替え

`https://bbs.example.org/notes/`を開き、ログイン→記事表示→小さいファイルのアップロード・ダウンロード→メール添付→チャットの順に確認します。補助端末は別のBBS接続なので、端末内でもログインします。Web端末内ではZMODEMを使わず、Webのファイル選択画面を使います。

| 差し替えるもの | 編集する場所 | 反映 |
| --- | --- | --- |
| Webの名称・バナー・案内・時間帯挨拶 | `web/messages/ja.json`、`en.json` | 次回の画面取得 |
| Web全体のHTMLとフォーム | `web/templates/index.html`、`components.html` | 次回のページ取得 |
| origin・接続先・上限 | `web/web.toml` | Webサービス再起動 |
| Telnet側のバナー・挨拶 | `/usr/local/share/liner-notes/` | BBS再起動 |

表の `web/` は `/usr/local/etc/liner-notes/web/`です。JSONはUTF-8で編集し、末尾のカンマ・引用符の欠落に注意します。HTMLの `id`、`data-*`、フォームの `name` は画面処理が参照するので保持します。設定・外部文書の詳細は [WebUIとApacheの構成・差し替え](web-server.md)にあります。

### 転送上限・再開の確認

転送上限は次のすべてを通過する必要があります。大きい値を一か所だけ設定しても、他の上限を超えると拒否されます。

| 場所 | この手順の値・役割 |
| --- | --- |
| Apache `LimitRequestBody` | 1073741824バイト（1GiB） |
| Web `max_upload_bytes` | 1073741824バイト、実装上限も1GiB |
| Web `max_spool_bytes` | 17179869184バイト、全Web転送作業領域の上限 |
| BBS `limits.conf` の `file_bytes` | 1073741824バイト |
| 公開保管庫・添付の各 `limits.conf` | 登録数・総容量・履歴件数の制限 |
| ファイルボード権限・ディスク | その会員の登録権限、両spoolと保存先の空き容量 |

「ブラウザーからの送信完了」と「BBSへの登録完了」は別です。登録中の進捗と最終結果を確認してから公開一覧を見ます。失敗した場合は保管庫の再開候補から対象を選び、同じファイルを選び直します。Web→BBSは確定済み位置から再開しますが、ブラウザー→WebのHTTP送信はファイルを送り直します。名前・サイズ・SHA-256の異なるファイルでは再開しません。

タイムアウトを増やす前に、どの段階で止まったか、BBS/Webのログ、空き容量、取得ファイルのSHA-256を確認します。小さいファイル、数十MiB、実際に使う大きさの順に試してください。1GiB×16・長時間の同時大量転送は0.74での検証完了を宣言していません。

<a id="web-troubleshooting"></a>
### Webのトラブルを層ごとに切り分ける

```sh
service liner_notes status
service liner_notes_web status
sockstat -4l -P tcp -p 23,2222,8088
tail -n 80 /var/log/liner-notes/daemon.log
tail -n 80 /var/log/liner-notes/web.log
tail -n 80 /var/log/httpd-bbs-error.log
```

| 症状 | 確認・対処 |
| --- | --- |
| ページ全体が `BBS connection unavailable` | `[bbs] host/port/telnet`、実runtime、接続枠、BBSが0.74かを確認。Webプロセスの起動だけではBBS接続成功を意味しない |
| `Unexpected Host` | `origin`のホスト名とHostヘッダー、ApacheのProxyPreserveHostを一致させる |
| `Origin rejected` / CSRFエラー | HTTPSの公開元を揃え、古い画面を再読込してログイン。検査自体を無効化しない |
| Apacheの403 | `apachectl -S`で対象VirtualHostを確認。`/`から`/notes/`への案内、Location、既存認証規則を確認 |
| 502・503でWebのログに到達しない | Webの8088待受とProxyPass先、Apacheログを確認 |
| `Web template unavailable` / `Web file access failed` | ファイルの存在・UTF-8・親ディレクトリの通過権限・Webユーザーの読み取りを確認。0.74はファイルの問題を500で区別する |
| `unrecognized arguments: -P ...` | 旧rc.dが残っている。0.74の `/usr/local/etc/rc.d/liner_notes_web`へ更新する |
| ログインできてもすぐ戻る | 公開origin、HTTPS、secure cookie、Web再起動によるセッション失効を確認 |
| アップロード413 | ApacheとWebの上限を確認。上の1GiBを超えていないか確認 |
| 登録中に止まる | BBS側の上限・権限・空き容量・最終照合時間、spool所有者を確認 |
| ゲームが見えない | BBSのdoors設定と会員レベルを確認し、ページを再読込。利用可能なゲームがなければ非表示が正常 |

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

## セッションと安全性

ブラウザーごとにBBS接続を1本、補助端末は追加1本使います。パスワードを保存・再利用せず、Web再起動時は再ログインします。Cookie・CSRFとWebSocketのOriginを検査します。BBSには接続元がlocalhostに見えるため、Apache側でも接続・レート制限を設置方針に合わせて設定します。WebアカウントへBBS原本やリンク秘密鍵の読み取り権限を与えません。結果不明の更新操作を自動再送しません。
