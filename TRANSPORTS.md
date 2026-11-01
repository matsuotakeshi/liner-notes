# 入出力と端末処理

現在の入力行上限は4,096バイトです。TelnetのNAWSで取得した列数・行数はページ表示に使います。通信はUTF-8、raw TCPまたはTelnetを選べます。外部転送プロセスとはバイナリ通信用の経路と認証済み操作用の経路を分離しています。

以下は初期設計の記録です。当時の上限・拡張予定は現行仕様ではありません。現在の導入方法はINSTALL.md、実施範囲はSTAGE70.mdを参照してください。

> この文書は機能追加時点の記録です。現行の導入入口は [README](README.md)、今回の変更は [STAGE65](STAGE65.md) を参照してください。古い段階名・上限・未実装事項は当時のものです。

# 第8段階: Telnetと入出力の分離

## FreeBSDで試す

```sh
make
make test
./runtime --telnet --menu 23230 notes.dat members.dat boards.example.conf
# 別端末から（表示文字コードはUTF-8）
telnet -8 127.0.0.1 23230
```

初期言語を英語にする場合は `--lang en` を追加します。LANG ja / LANG enによる切替も継続して使えます。`--telnet` を省略すれば従来のraw TCPです。RAW / MENUは表示方式の切替なので、Telnet接続中もTelnet制御は維持されます。

第31段階で `--bind IPv4` による外部Telnet待受と、OpenSSH経由の入口を追加しました。詳細は [STAGE31.md](STAGE31.md) を参照してください。Telnet自体は暗号化していません。

## 責務と将来の拡張

```text
TCPソケット ── raw / Telnet ── 行入力 ─┐
COMドライバ（将来）── plain端末・行入力 ├─ Session ── Board worker / 保存
Webアダプター（将来）── 完成した入力行 ─┘       │
                                  UTF-8表示 + secret入力状態
```

| 層 | 実装 | 役割 |
| --- | --- | --- |
| 掲示板セッション | session.c / session.h | コマンド、会員、現在ボード・記事、投稿中の状態、タイマー、ワーカー結果の受け取り |
| 表示 | ui.c / messages.c / locales | 日本語・英語のメニューやメッセージ |
| 行入力 | terminal.c / terminal.h | raw行入力、端末の改行・削除操作・エコー制御 |
| Telnet | telnet.c / telnet.h | IACの解釈、オプション交渉、送出時のエスケープ |
| OS入出力 | runtime.c | TCP待受、poll、受信・送信キュー、接続の寿命と公平な処理 |

Sessionにはソケット、ファイル記述子、Telnet状態を持たせていません。`SessionIO.write(context, text, size)` にUTF-8表示を書き出し、`session_line()` に改行を除いた入力行を渡します。`session_secret()` はパスワード入力中かどうかを知らせます。コード上の構造だけでなく、ソケットなしのメモリ入出力でも実際にセッションを動かす試験を追加しています。

COMでは、OS固有のポート設定・読書き・切断検出を新しいアダプターに実装し、既存の `TERMINAL_PLAIN` 行入力処理を使用できます。ボーレート、フロー制御、モデム制御、文字コード変換はそのアダプターの責務です。今回、COMドライバや実機通信は実装していません。

Webでは、WebSocketのテキスト端末、または入力フォームから `session_line()` へ渡す構成を取れます。描画にはtextContent等で表示テキストを扱い、HTMLとして解釈しないでください。HTTP/WebSocketの受付、TLS、接続認証、Origin確認、ブラウザ側の入力部品はWebアダプターで追加します。構造化されたWeb画面を作る場合は表示用プレゼンターを別途追加します。今回、WebサーバーやWebSocketは実装していません。

### アダプターの契約

- Session関数はイベントループから直列に呼びます。保存処理は既存のワーカーへ送ります。
- 書出しコールバックは、渡されたデータを戻るまでにコピーします。有限キューが満杯なら0を返し、セッションを終了させます。Session内部の文字列へのポインターを保持しないでください。
- 呼出し側で送信キュー量を見て入力を止め、`session_ready()` が真のときに入力行を1つずつ渡します。受付前の行は呼出し側で保持します。
- パスワードを含む入力は記録・エコーしません。secret状態は受信パケット単位ではなく、各行の処理後に更新し、次の文字を扱う前に反映します。
- `session_tick()` に単調時計を渡し、`session_result()` にワーカー結果を渡します。接続ごとのslotと世代IDで、切断前の結果を新しい接続へ流さないようにします。
- 会員停止・解除の通知は管理操作の要求元が切断済みでも反映します。`account_user/account_epoch` による接続取消しと遅れた認証結果の扱いは [会員管理のアダプター契約](MEMBERS_ADMIN.md) を参照してください。
- `closing` 時は既存の出力を排出して切断し、異常時は `session_abort()`、終了後は `session_dispose()` を使います。
- 表示の書出しは現在の実装と同様にCRLFを分割しないテキスト単位で渡します。Webのフレーム分割やソケットの部分書込みは、その後段で処理します。

## Telnetで対応した範囲

- 分割して届くIAC、WILL/WONT/DO/DONT、サブネゴシエーションの状態保持。
- ECHO、SGA、双方向BINARY、NAWSの交渉。重複要求で応答ループを作らない処理。
- IAC IACによるデータ255の受信・送信。NVT送出時のCRLF／CR NUL、BINARY合意後の8ビット送出。
- NAWSの幅・高さを保存します。現在の一覧は固定レイアウトで、自動折返しやページ行数調整にはまだ使いません。
- 端末入力としてCRLF、CR NUL、LFを受け付けます。CRをEnterとして扱います。
- Backspace / Delete / Telnet ECで1コードポイント、Ctrl-U / Telnet ELで入力行を削除。日本語の一般的な全角文字幅も扱います。
- 有限長のANSIカーソルキー列は無視します。カーソル移動・履歴・全文編集は未実装です。結合文字・絵文字の書記素単位削除や厳密な表示幅は今後の対象です。
- パスワード入力中はサーバーエコーを止めます。ECHOを拒否するTelnetクライアントは、パスワード受付に入る時点で切断します。
- 入力行は1024バイト、Telnetサブネゴシエーションは1024バイトで上限を設けます。制御コードや未完の通信で他の接続を止めません。

UTF-8の8ビット端末を前提とした実用的なサブセットです。CHARSET、LINEMODE、端末種別、OOB/SYNCH、半二重GA制御、旧文字コードの自動判定は未対応です。

参照仕様: [RFC 854](https://www.rfc-editor.org/rfc/rfc854)、[BINARY: RFC 856](https://www.rfc-editor.org/rfc/rfc856)、[ECHO: RFC 857](https://www.rfc-editor.org/rfc/rfc857)、[SGA: RFC 858](https://www.rfc-editor.org/rfc/rfc858)、[NAWS: RFC 1073](https://www.rfc-editor.org/rfc/rfc1073)。

## 試験

既存7系列に加えて、メモリアダプターのC試験、Telnet実ソケット試験、POSIXの標準TelnetクライアントをPTYで操作する試験を用意しました。標準クライアントがない環境では最後の試験はSKIPと表示します。

2026-10-26: FreeBSD 15.1-RELEASE amd64の標準ccで警告をエラー扱いにしたビルドと全10系列の試験が成功しました。標準TelnetクライアントのPTY試験もSKIPなしで成功しています。WindowsではPOSIX専用試験を除く9系列が成功しました。

FreeBSD検証場所: `/home/takeshi/notes-stage8-20261025-a`。結果は [freebsd-stage8.txt](evidence/freebsd-stage8.txt) に記録しています。主要10ファイルのローカル／検証機SHA256も一致しました。試験サーバーは終了済みです。

第12段階では `PASSWD` の3段階入力も共通Sessionで扱います。パスワード変更時の要求元接続の継続と他接続の失効については [PASSWORD.md](PASSWORD.md) のアダプター契約を参照してください。

第13段階の [記事検索](SEARCH.md) も共通Sessionと保存ワーカーで処理します。rawでは `SEARCH` / `SEARCHFROM`、対話メニューでは `X` と `+` を使用します。

第14段階の [記事ページ送り](PAGER.md) はSession内で動作します。COMなどのアダプターから `session_dimensions()` で画面サイズを指定できます。TelnetはNAWSの変更を伝えます。raw応答は分割しません。

## stage36の送受信コネクタ

ファイル送受信は [TRANSFER.md](TRANSFER.md) の共通コネクタ契約を使用します。stage37ではline-v1に加え、端末の行編集を迂回する [binary-v1専用転送モード](BINARY_TRANSFER.md) と保管庫ワーカーを追加しました。ZMODEMエンジンは未実装です。

## stage38のZMODEM試作

[ZMODEM.md](ZMODEM.md) のSSH用入口は、8ビット透過の標準入出力を外部lrzszプロセスへ渡します。内部のBBS会員接続は別ソケットで維持します。runtimeのTelnet入出力に直接ZMODEMを混ぜる実装は、まだ行っていません。

## stage39のチャット

[CHAT.md](CHAT.md) の共通チャット室は、会員認証後の要求だけをイベントループで扱います。チャット中の接続だけに通知し、通常の掲示板・パスワード入力・バイナリ転送へ非同期メッセージを流しません。会話本体は通信FDも会員DBも持たず、通知コールバックでSessionへ届けます。
