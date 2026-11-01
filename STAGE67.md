# Liner notes stage67

## 操作と権限

- `WITHDRAW` / 読書中の `K` は投稿者本人、管理者、親フォーラムから継承したSIGOPが実行できます。履歴には実際の操作者を記録します。別ボードの担当者には権限を与えません。
- 記事の `G` / `GET` は閲覧できる本文を共通編集バッファへ保存します。投稿権限は不要で、投稿エディタは開始しません。`BUFFER` で確認・編集し、記事・メールの編集時に `/BUFFER` で使います。
- 記事とメールの本文表示中は Enter=次、P=前、L=再表示、F=無停止。メール一覧へは `MAIL LIST`。`+/-`、`+N/-N` は番号順の方向・相対移動です。
- メニューの O/Q はY/N確認後に切断。空入力は中止。明示的な `QUIT` は確認なしです。ページ停止や連続表示中のQは表示の中止を維持します。
- Sは外部 `sysinfo.txt`、Vは版情報。終了時に外部 `goodbye.txt` を表示します。両ファイルはUTF-8・各4096バイトまで。欠落・空ファイルなら起動できます。内容は起動時に読み込みます。
- 個人設定画面のDは実際の設定値を表示。管理者の `$` は会員・掲示板・保管庫・接続・保守の目的別メニューへ進みます。管理HELPは一般会員に表示しません。

## 日時と巡回

`--timezone Asia/Tokyo` のようにサーバーのIANAタイムゾーンを指定します。既定UTC。保存値はUNIX時刻のままです。日付検索のYYYY-MM-DDは指定ゾーンの午前0時、履歴表示はそのゾーンの時刻とオフセットになります。誕生日は暦日として扱います。

日付を空入力した記事Nは現在ボード、保管庫Nは選択したファイルボードと配下、メールNは本人の受信メールです。`CHECK ARTICLES HERE|SUBTREE|ALL` / `CHECK MAIL` / `CHECK FILES パス` は種類・範囲ごとに前回確認日時を保存します。

メニュー接続では最後の結果を最後の表示ページまで出した後、確認開始時刻を自動保存します。途中のQ・CHECK CANCEL・切断は未完了扱いです。確認中に届いた項目を次回見逃さないよう、終了時刻ではなく開始時刻を保存します。`CHECKMODE MANUAL` は従来の `CHECK DONE` による確定、`AUTO` は自動。RAW接続の初期値はMANUALです。

## 保管庫と添付

`FILEBOARD games` で現在位置を選択（ルートは `-`）。L/I/U/S/Nに引き継ぎます。`FILESEARCH games|*.zip` / `FILENEWS games|2026-10-01` はその配下に絞ります。閲覧権限・パスワードを通過したものだけを表示します。

U/UBは転送方式→説明→XMODEM/TEXTの場合は名前・サイズを質問します。Bから選択追加・削除・ワイルドカード・一覧・クリア・送信へ進めます。既存の名前付きコマンドも使えます。

`MAILUPLOAD` / `MAILUPLOADBATCH` / `MAILDOWNLOAD` / `MAILDOWNLOADALL` はPROTOCOLで選んだ方式を使用。アップロード引数は説明、XMODEM/TEXTだけは `名前|バイト数|説明`。ダウンロード引数はファイル番号、一括取得はメール番号。Zで始まる従来コマンドはZMODEM固定です。XMODEM/TEXTは単一ファイル方式です。添付の権限は全方式で送信者・受信者だけ（SYSOP例外なし）。

## 設定できる運用上限

`--limits limits.conf` で `limits.example.conf` の9項目を指定します。会員数・ボード番号・入力長・本文長・ファイル容量・一括件数/容量・同時接続/転送です。小さくしても既存データの読出しを拒否しません。新規登録・追加・転送に適用します。転送上限は保管庫/添付と各方式で共通です。

値は正整数で、現在の安全上限（会員999999、ボード1024、入力4096、本文262144、ファイル1GiB、一括16/16GiB、接続128/転送24）以内です。未知項目・重複・0・超過は起動時に拒否します。これらはメモリやプロトコル表現を保護するため無制限にはしません。累計記事・メール・投稿者別件数は既存オプションの0=無制限を維持。保管庫の累計ID・総容量・イベント件数は既存のvault-limits設定を使います。

## 正式なSQLite保存と移行

`--database /var/db/liner-notes/liner.sqlite` を明示すると、記事・メール・会員・プロフィール・操作履歴等の原本をSQLiteに保存します。既存の設定DBも取り込みます。SQLiteのWALとFULL同期を使用し、確定応答前にコミットします。同一DBの二重起動は拒否します。

原本の記録形式を明示的なバイト列として4KiBブロックに分割しSQLite内へ保存する方式です。C構造体のメモリを保存しません。設定は同じDB内のテーブルです。記事等を全て個別のSQL列に展開する設計ではありません。検索用SQLite索引は再作成可能な別ファイル、保管庫と添付の実ファイル、外部設定、daemonの標準出力ログは引き続きファイルです。従来原本の全体ハッシュはDBの識別子・更新番号による判定へ置き換えました。索引再構築や保守の全件走査は残ります。

既存サービスを止めてから、まず別コピーを作ってください。本番切替を自動では行いません。

```sh
liner-notes-admin db-migrate --root /var/db/liner-notes \
  --board notes.dat --members members.dat /var/db/liner-notes-new
```

出力は新しいディレクトリのみ許可し、原本を変更しません。全ストリームをSHA-256で照合します。旧保存ファイルも移行先に残ります。`--database` 付きで移行先を起動し、設定・外部ファイルのパスも移行先へ合わせて確認してください。DB指定なしなら従来形式のままです。稼働中の旧形式と新DBを同じ実ファイル領域で同時運用しないでください。

停止中のDB利用環境をバックアップ・復元・整理する例です。

```sh
liner-notes-admin db-backup --root /var/db/liner-notes \
  --database /var/db/liner-notes/liner.sqlite \
  --board notes.dat --members members.dat /backup/liner.tar.gz
liner-notes-admin verify /backup/liner.tar.gz
liner-notes-admin db-restore /backup/liner.tar.gz /var/db/liner-notes-restored
liner-notes-admin db-compact /backup/liner.tar.gz /var/db/liner-notes-clean \
  --all-stores --reclaim-withdrawn
```

`db-backup` はDBから現行記録を論理出力し、実ファイルを含む検証可能なアーカイブを作ります。旧backupによるDB環境の誤バックアップは拒否します。`db-compact --reclaim-withdrawn` は別コピーで取り消し本文を物理回収し、記事番号・欠番・取り消しと操作者の履歴を維持します。保存済みバックアップにある本文は削除しません。復元にはアーカイブ全体が必要です。

`db-export --root ROOT --database DB --board notes.dat NEWDIR` で現行DBを旧形式の別コピーへ出力できます。DB親ディレクトリ外の論理ログは `external/元の絶対パス` 以下へ出力します。移設時は設定したログパスとの対応も確認してください。

## portsとrc.conf

既存の設置階層を維持します。`liner_notes_database`、`liner_notes_timezone`、`liner_notes_limits`、`liner_notes_sysinfo`、`liner_notes_goodbye` をrc.confで指定できます。DBは明示的に設定した場合だけ有効です。例:

```sh
liner_notes_database="/var/db/liner-notes/liner.sqlite"
liner_notes_timezone="Asia/Tokyo"
liner_notes_limits="/usr/local/etc/liner-notes/limits.conf"
liner_notes_sysinfo="/usr/local/share/liner-notes/sysinfo.txt"
liner_notes_goodbye="/usr/local/share/liner-notes/goodbye.txt"
```

設定・案内はsampleとして配布し、編集済みファイルを上書きしません。追加HELPとメニューを導入し、既存のHELP本文の旧キー説明も新sampleと比較して更新してください。

旧RTBBSとの画面配置・全プロンプトの完全一致、旧DOSドアやネットワーク連携はこの変更の対象外です。検証の実施範囲は `evidence/stage67/VERIFICATION.md` を参照してください。
