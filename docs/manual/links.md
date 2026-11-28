# BBS間リンクとスター・リング型共有

リンクはBBS本体と別のプロセスです。指定ボードの記事・返信・題名変更・
取り消しだけを交換します。会員データ、認証情報、私信、メール添付、保管庫は
交換しません。既定では無効です。FidoNet互換ではありません。

## 共有単位と権限

- BBSの識別子はEd25519公開鍵のSHA-256（64桁）。IPアドレスとは独立です。
- 共有ボードは64桁の共通識別子を使い、各BBSのローカルボード番号へ割り当てます。
- `peers`は直接通信する相手、`origins`は記事の発信元として許可する相手です。
  中継相手を許可するだけでは、その先の発信元を自動では信頼しません。
- 接続はTLS 1.3の相互証明書認証、記事は発信元による署名を検証します。
  証明書と鍵の指紋は、接続URLとは別の確認済みの方法で照合してください。
- 本体側は専用の**管理者ではない会員**をボード番号ごとに明示許可します。
  閲覧・投稿権限も必要です。親SIGOPや他ボードの指定からは継承しません。
  ボード共通パスワードの自動入力には対応しません。共有専用ボードを用意します。
- 投稿者は `@発信元8桁:公開名` として表示し、ローカル会員と区別します。
  元の記事番号は保存せず、発信元・記事識別子からローカル番号へ対応付けます。

共有したボードの既存記事も送信対象です。非公開情報を含む既存ボードを
共有用へ転用しないでください。共有開始前に取り消した記事は本文を送らず、
返信関係を保つための取り消し済み親を作ります。すでに相手へ渡した記事を
取り消しても、相手のバックアップから物理消去される保証はありません。

## パッケージとOSアカウント

BBS上のリンク専用会員は、ローカルBBSへログインして指定ボードを操作するための会員です。OSアカウント `liner_notes_link` はリンクプロセスを動かし、鍵とリンクDBを所有します。OSアカウントへBBS本体の保存領域を公開する必要はありません。パッケージ導入だけではサービスを有効にしません。

| 対象 | パッケージ | 設定ディレクトリ | 状態ディレクトリ |
|---|---|---|---|
| FreeBSD | `pkg install ./liner-notes-版.pkg`（Portsでは `LINK` を選択） | `/usr/local/etc/liner-notes` | `/var/db/liner-notes-link` |
| Rocky Linux 10 | `dnf install ./liner-notes-版.x86_64.rpm ./liner-notes-link-版.x86_64.rpm` | `/etc/liner-notes` | `/var/lib/liner-notes-link` |
| Debian 13 | `apt install ./liner-notes_版_amd64.deb ./liner-notes-link_版_all.deb` | `/etc/liner-notes` | `/var/lib/liner-notes-link` |

上記コマンドのファイル名は同じリリースの実ファイル名に置き換えます。FreeBSDで `liner_notes_link` のrc.dが同梱されていない旧パッケージでは、共通CLIを専用アカウントで使用します。Linuxのソース導入・EL9は[Linuxへの導入](install-linux.md)を参照してください。従来のPythonスクリプト直接実行も使用できます。

```text
会員 → BBS本体 ← ローカルTelnet／BBS専用会員 ← Linkサービス
                                            ├─ 専用OSユーザー／鍵・リンクDB
                                            └─ 相互TLS → 対向Linkサービス → 対向BBS
```

## 初回構築

1. 各局で空の共有専用ボードを作ります。番号、親階層、読み書き権限、表示名、説明を決めます。公開範囲は参加局すべてで確認します。リンク専用の一般会員（例 `bbslink`）を作り、該当ボードの閲覧・投稿を許可します。各局の番号は独立です。
2. 設定ディレクトリの `link-accounts.conf` に、許可する `ローカルボード番号 会員番号` を各行に記載します。下記のどちらかで本体へ設定します。既存のボード設定や起動引数は残します。

   ```sh
   # FreeBSD
   sysrc liner_notes_link_accounts=/usr/local/etc/liner-notes/link-accounts.conf

   # Linux: /etc/liner-notes/liner-notes.env の LINER_NOTES_OPTIONS で
   # ポート引数の直前より前に次の2引数を挿入
   # --link-accounts /etc/liner-notes/link-accounts.conf
   ```

3. `id liner_notes_link` で専用OSユーザーを確認します。状態ディレクトリは専用サービスが所有する0700とします。以下はLinuxの例です。FreeBSDでは `state=/var/db/liner-notes-link` とし、`runuser` の行をその直後の `su` の例に置き換えます。

   ```sh
   state=/var/lib/liner-notes-link
   runuser -u liner_notes_link -- liner-notes-link init "$state/identity" --name "Example BBS A"
   # FreeBSD:
   # su -m liner_notes_link -c '/usr/local/bin/liner-notes-link init /var/db/liner-notes-link/identity --name "Example BBS A"'
   liner-notes-link inspect "$state/identity/identity.crt"
   liner-notes-link new-board
   ```

   `init` は既存領域を上書きしません。共有IDは参加局のうち1局で発行し、対象局で同じ値を使用します。各局で交換するのは `identity.crt` だけです。署名鍵の指紋は別の信頼できる経路で照合します。相手の公開証明書を設定ディレクトリの `link-peers/` に管理者が置き、サービスへ読み取りだけを許可します。
4. BBS専用会員のパスワードを状態ディレクトリの `bbs-password` に1行で保存します。専用OSユーザー所有・0600とし、端末の履歴やコマンドラインに値を残さない方法で入力します。秘密鍵も0600とします。
5. `link.toml.sample` を参照して `link.toml` を編集します。パス、ローカルBBSのポート、共有ID、各局の証明書・接続先を実設定へ置き換えます。設定は管理者所有で専用グループに必要な読み取りだけを許可します。LinuxパッケージのローカルBBSポート例は2222です。FreeBSDでも実際の `liner_notes_port` と合わせます。
6. 待受を対向局が到達できるIPアドレスに設定し、対向局からのTCP通信だけをファイアウォールで許可します。`0.0.0.0` は全IPv4インターフェースで待ち受けます。NATを使用する局では必要なポートだけを転送します。相互TLSを構成した後も不必要な公開は避けてください。
7. 専用OSユーザーで `liner-notes-link check --config 設定ディレクトリ/link.toml` を実行します。`check` は設定と鍵を検査します。BBSログイン、TLS疎通、書き込み可能な状態領域は起動後に確認します。
8. バックアップと告知を済ませて本体の設定を反映し、[リンクサービスを起動](link-operations.md)します。全局で別々のテスト記事を投稿し、別局で返信します。全局で本文・返信先・件数を確認し、重複と保留が解消したら自動起動を有効にします。

## link.toml リファレンス

パスは文字列で指定し、サービスから見える絶対パスを使います。下表の既定値はCLI実装の値です。サンプルの巡回予算や鍵更新設定は構築例です。

| 欄 | 型・既定値 | 条件と影響 |
|---|---|---|
| `node.identity` | パス、必須 | `identity.key` と `identity.crt` を格納。鍵と証明書は同じノードを示すこと |
| `node.database` | パス、必須 | SQLite、WAL・SHM・排他ロックを作成できる専用ディレクトリ |
| `node.listen` | 文字列、`127.0.0.1` | 待受IP。遠隔局からの通信には到達可能なIPを指定 |
| `node.port` | 整数、9044 | 1～65535。OSが許可する非特権ポートを選択 |
| `node.interval` | 整数、5 | 1～3600秒。各巡回の後の待機時間 |
| `node.rotate_tls` | 真偽値、false | trueで通信用TLS鍵を更新。発信元署名鍵は維持 |
| `node.control_socket` | 絶対パス、指定なし | ローカル管理用Unixソケット。Linuxは `/run/liner-notes-link/control.sock`、FreeBSDは `/var/run/liner-notes-link/control.sock` を利用可能 |
| `node.scan_budget` | 整数、128 | 1～8192件、1ボード・1巡回当たり |
| `node.pull_batches` | 整数、16 | 1～1024回、対向局・1巡回当たり |
| `node.max_events` | 整数、1000000 | 1以上。満杯ならデータを保持して再試行 |
| `node.max_bytes` | 整数、1073741824 | 最小1048576バイト。履歴の自動削除は行わない |
| `bbs.host`、`bbs.port` | 文字列 `127.0.0.1`、整数23 | ローカルTelnetの接続先。実際の本体待受に合わせる |
| `bbs.login`、`bbs.password_file` | 空白なし文字列とパス、必須 | リンク専用会員と、その0600の認証ファイル |
| `boards[].id`、`boards[].local` | 64桁の16進文字列と1～1024の整数、必須 | 共通IDとローカルボード。少なくとも1組、双方の重複不可 |
| `origins[].certificate`、`origins[].boards` | パスと共有IDの配列 | 許可する発信元ごとに指定。各配列は空にできず、ローカルで設定したIDだけを許可 |
| `peers[].certificate`、`peers[].boards`、`peers[].host` | パス、共有IDの配列、文字列、必須 | 直接接続する各相手に指定。最低1局。証明書が示す自局IDや重複IDは不可 |
| `peers[].port` | 整数、9044 | 1～65535。対向局の実際の待受ポート |

`inspect` は公開証明書のノードIDを出力します。`serve` はフォアグラウンドで実行するため、通常はサービス管理から起動します。`status` はリンクDBの状態を読み取ります。保守コマンドは[管理・保守手順](link-operations.md)を参照してください。

## 2局・3局の構成例

- **2局**　A ↔ B。各局の `peers` と `origins` に相手を1件登録します。
- **3局**　A ↔ B ↔ C ↔ A。各局の `peers` と `origins` に自局以外の2局を登録します。各組で同じ共有IDを許可し、相手の設定にも自局を登録します。
- **3局スター**　Aを中継局とし、BとCはAだけを `peers` に登録します。`origins` には、記事の送信元として許可する両局をそれぞれ登録します。

例えば、Aでは `peer-b.example.test` と `peer-c.example.test`、Bでは `peer-a.example.test` と `peer-c.example.test` を接続先にします。実際に各局が到達できる名前に置き換えてください。返答が別局に同じ番号で現れる必要はありません。発信元IDと記事IDから各局の親記事へ対応付けます。4局以上の多段中継はこの運用例の検証範囲外です。

## 状態・復旧・バックアップ

```sh
liner-notes-link status --config /usr/local/etc/liner-notes/link.toml
```

`pending` は未適用イベント、`deferred` と `retry_reasons` はBBS側で拒否されて
再試行待ちの記録です。権限、ボード容量、親記事の管理状態を確認します。
通信失敗時のカーソルは、署名検証と保存が完了した分だけ進みます。
1回の交換は最大4イベント、受信枠4MiB、同時受入8接続です。
キュー上限は `max_events`（既定100万）と `max_bytes`（既定1GiB）。
無制限にはせず、満杯では削除せず停止・再試行します。履歴の自動間引きはありません。

本体データとリンクDB・秘密鍵は別領域です。整合するバックアップを作るときは
リンクを止め、BBSを止めて管理ツールでバックアップし、リンクDBをSQLiteの
backup APIで別コピーへ保存します。稼働中のsqliteファイル単体コピーは避けます。
復元では同じ公開鍵と対応するリンクDBを使います。キーを新規生成すると別BBS扱いです。

対向が古いリンクDBへ復元された場合は「カーソルが保存範囲より先」のエラーが
出ます。復元内容を確認し、取得する側のリンクを停止してから次を実行します。

```sh
liner-notes-link reset-peer --config /usr/local/etc/liner-notes/link.toml 相手の64桁指紋
```

これで再取得します。既存イベントの重複は抑制します。稼働中のリセットは
排他ロックで拒否します。BBSを古いコピーへ戻す場合は、リンクDBも対応する
時点へ戻してください。本体だけを戻し、リンク側の適用済み記録を残すと
未復元記事が自動再適用されないためです。

## 現在の境界

共有の対象は投稿・返信・題名・取り消しです。移動・投稿者変更・非表示は
ローカルの管理操作です。受信記事の再編集・再公開を発信元へ戻す双方向編集は
しません。全サイト共通の即時トランザクションではなく、復旧後に順次同期します。
この版では、署名付きチェックポイントによる過去イベントの物理回収、通信専用TLS鍵の
自動更新、新規参加向けスナップショット、管理者用Web状態表示・停止／再開を提供します。
全ノードをprotocol 2対応版にそろえます。[管理・保守手順](link-operations.md)を参照してください。
大量の既存記事の初回同期は、ボード単位の順次走査になるため時間がかかります。
1周期の送信元走査は既定で1ボード128件までとし、次の位置を保存します。上限は設定可能です。
変更検出もこの巡回で行うため、大規模ボードでは即時反映になりません。
変更のない記事本文は読み直しません。初回共有では小さな専用ボードから始めます。

Solaris 11.4 x86-64では専用venvを用いて、3局スター・
リング、mTLS、再送、再起動、チェックポイントを実機CIで確認しました。
設定例の保存先は `/var/opt/liner-notes/link` です。
パッケージによる導入範囲は[Solarisへの導入](install-solaris.md)を参照してください。

## 削除伝播とローカル非表示

| 操作する局・利用者 | 操作 | 伝播 |
|---|---|---|
| 投稿元の本人・権限ある管理者/SIGOP | WITHDRAW | 発信元署名付き取り消しとして全局へ伝播 |
| 受信先の管理者/SIGOP | HIDE / RESTORE | 自局だけ。送信イベントにしない |
| 権限のない利用者 | WITHDRAW / HIDE / RESTORE | 拒否 |

親の取り消しは、その配下の全記事を不可視にします。他局が投稿した返信も対象です。子孫の本文・親子関係・識別子を保持し、子孫の発信元を偽装する取り消しイベントは生成しません。非表示の親へ新たに到着した署名済み返信も保存し、祖先の状態から表示を判定します。LINKSNAP/LINKSCAN/LINKPAGEは不可視記事の題名・本文を返しません。同期用の発信元・記事識別子・親番号・版は保持します。

protocol 2、Ed25519署名、チェックポイントとスナップショットの形式は変更しません。ただし**旧0.74.1以前のノードは親取り消し後も返信を表示するため、共有する全局のBBS本体をこの修正を含む版へ更新してから、全局での子孫不可視を運用上保証してください**。通信形式が同じでも表示規則は同じとは限りません。旧ノードとの自動的な版識別・拒否は行いません。

ローカル非表示は相手のBBSや既に保存されたリンクイベント、バックアップの内容を消しません。HIDEした親配下は新規送信走査から除外されますが、既に署名・送信キューへ保存したイベントは従来どおり中継されます。新規未送信の記事も共有したい場合は、その局での非表示を解除してから走査します。

## English setup guide

Install the `liner-notes-link` package alongside the main RPM on Rocky 10 or the main DEB on Debian 13. On FreeBSD, use the package built with `LINK` or enable that option in Ports. Linux uses `/etc/liner-notes` and `/var/lib/liner-notes-link`. FreeBSD uses `/usr/local/etc/liner-notes` and `/var/db/liner-notes-link`. Older FreeBSD packages without the rc.d file support the same foreground CLI.

1. Create an empty shared board at each site. Use the board's existing read/write ACL and create a non-admin BBS member with access to that board. In `link-accounts.conf`, add one pair of local board number and BBS member number per line. The local numbers may differ between sites.
2. Set `liner_notes_link_accounts=/usr/local/etc/liner-notes/link-accounts.conf` with `sysrc` on FreeBSD. On Linux, insert `--link-accounts /etc/liner-notes/link-accounts.conf` before the positional port in `LINER_NOTES_OPTIONS` in `/etc/liner-notes/liner-notes.env`.
3. As the dedicated OS account `liner_notes_link`, run `liner-notes-link init STATE/identity --name "Example BBS A"`. The OS account owns the private state, while the separate BBS member authenticates over local Telnet. Exchange each site's `identity.crt` and verify the public key fingerprint out of band with `liner-notes-link inspect FILE`. Keep `identity.key` private. Generate a single shared board ID with `liner-notes-link new-board`.
4. Save the BBS member password as one line in `STATE/bbs-password` with mode 0600 and ownership by the dedicated OS user. Copy the supplied sample into `link.toml` if needed. Set the identity and DB paths, the BBS endpoint and member, and map the shared ID to the local board. Put the verified public certificates in the config directory under `link-peers/`.
5. Register the other node under both `origins` (signing trust) and `peers` (mutual TLS and TCP endpoint). With two sites, each registers the other. In a three-site bidirectional ring, each registers both other sites and allows the same board ID. For a three-site star, leaf nodes use the hub as their direct peer and explicitly trust all permitted signing origins. In all cases, signed replies resolve to the destination's local parent article.
6. Set the listening IP and allow the configured TCP port from the peers. Binding `0.0.0.0` listens on every IPv4 interface. Use narrow firewall and NAT rules. Confirm the material with `liner-notes-link check --config CONF/link.toml` as the link OS user, then apply the BBS settings and start the link with the commands in [English operations guide](link-operations.md#english-operations-guide). Verify posts in both directions and a cross-site reply before enabling automatic startup.

| Settings | Defaults and constraints |
|---|---|
| `node.identity`, `node.database` | Required paths. The identity directory holds both matching identity files. Use an owner-only state directory that supports SQLite WAL/SHM and the process lock. |
| `node.listen`, `node.port` | `127.0.0.1`, 9044. Ports are integers 1–65535. |
| `node.interval`, `node.rotate_tls`, `node.control_socket` | 5 seconds (1–3600), false, and absent. Rotation changes the TLS leaf while retaining the signing key. If set, the control socket path must be absolute. |
| `node.scan_budget`, `node.pull_batches` | 128 articles per board (1–8192), 16 pulls per peer (1–1024). |
| `node.max_events`, `node.max_bytes` | 1000000 events (minimum 1), 1073741824 bytes (minimum 1048576). Capacity exhaustion stalls exchange and retains existing data. |
| `bbs.host`, `bbs.port`, `bbs.login`, `bbs.password_file` | `127.0.0.1`, 23 in the CLI (packaged BBS example 2222). Login and secret path are required. The login may not contain whitespace. |
| `boards[].id`, `boards[].local` | Required 64-digit hex ID and local number 1–1024. At least one row. Use each ID and local board only once. |
| `origins[].certificate`, `origins[].boards` | Public certificate and nonempty list of permitted configured board IDs for each signing origin. |
| `peers[].certificate`, `peers[].boards`, `peers[].host`, `peers[].port` | At least one peer. Public certificate, board list and endpoint are required. Port defaults to 9044. Do not register the local identity as a peer. |

`check` validates local configuration and key material. Confirm actual BBS login, mutual TLS and article exchange after startup. `serve` runs in the foreground for operator-managed installations. Only shared articles and their allowed changes are exchanged. Review existing board contents before sharing and agree on publication scope at all participating sites.
