# Liner notes stage61 — 保管庫・会員・共通エディタ・外部HELP

stage60からの追加です。旧RTBBSのソースを参照していますが、旧データ形式と全端末の完全互換を保証するものではありません。掲示板と保管庫は引き続き別モジュールで、転送エンジンには会員データを渡しません。

## ビルドと起動

FreeBSDでは標準のCコンパイラ、make、OpenSSLのヘッダー／libcryptoを使用します。試験と転送コネクタにはPython 3が必要です。configureは不要です。

```sh
make
make check
./runtime --telnet --menu --lang ja --menu-dir menus --help-dir help \
  --banner banner.txt --admins admins.conf 2222 notes.dat members.dat
```

`admins.conf` は実際の管理者会員番号を設定してください。上の例はlocalhostで待ち受けます。外部接続は従来どおり `--bind 0.0.0.0`、保管庫は `--vault-dir /absolute/path/vault`、転送は設定済みの `--zmodem-helper /absolute/path/helper` を追加します。

`make` はruntimeとvaultctlを作成します。`make check` は従来の回帰試験とstage61試験です。`make clean` は生成実行ファイルだけを除去し、会員・記事・設定を消しません。`CC`、`CFLAGS`、`CPPFLAGS`、`LDFLAGS`、`LDLIBS`を指定できます。

実転送の追加試験は、修正版lrzsz 0.13.1の絶対パスを指定します。

```sh
make test-legacy-transfers LRZ=/path/to/lrz LSZ=/path/to/lsz
make test-zmodem LRZ=/path/to/lrz LSZ=/path/to/lsz
```

既存rc.dも新しい設定に対応しています。項目は `liner_notes.rc.conf.example` を参照してください。既存サービスの切り替えは自動では行いません。

## 外部HELP、バナー、管理者メニュー

- HELP本文は `help/ja/*.txt` と `help/en/*.txt`。各16項目、計32ファイルです。UTF-8、各16KiBまで、通常のテキストとして編集できます。
- `index.txt` が入口。`editor.txt` は `HELP EDITOR` と編集中の `/?` に共用します。`HELP TALK` は `chat.txt` を使用します。
- 起動時に読み込みます。変更後はBBSを再起動してください。改行LF/CRLF、UTF-8 BOMに対応します。
- 引数なしでは作業ディレクトリの `help/` を読み、不足するファイルは内蔵HELPで補います。`--help-dir DIR` を明示した場合は日英の全ファイルを検査し、欠落・空・不正形式は起動エラーにします。
- `HELP ADMIN` は全体管理者のみ、`HELP SIGOP` は全体管理者／掲示板SIGOPのみ。外部本文を差し替えてもこの判定は変わりません。一般向けHELPファイルに管理者用本文を書き込まないでください。
- 新しい `banner.txt` に指定のロゴを収録しました。未配置・空ファイルならバナーを省略します。
- 外部メニュー内の `[ADMIN_MENU]` の位置へ権限別メニューを差し込みます。標準配置は `[案内・終了]` の直後、区切り線と使い方案内の前です。古いメニューにマーカーがなければ末尾へ補います。

## ファイル保管庫

| 操作 | 内容 |
|---|---|
| `FILEOWNER ファイル番号 会員番号` | 登録者変更。全体管理者またはそのファイルボードの担当者 |
| `FILEBOARDSIGOP パス\|会員番号` / `パス\|NONE` | 保管庫担当者の設定／解除。全体管理者のみ |
| `FILEASCII 番号 ON` / `OFF` | テキスト属性。登録者または管理者／担当者 |
| `FILETYPE 番号 [バイト位置]` | テキスト直接表示。メニューの `+` で続き。制御文字は無害化 |
| `FILESEARCH *.LZH` / `file?.txt` | ファイル名のワイルドカード。英字大小文字は同等 |
| `BATCH MATCH *.LZH` | 一致する取得可能ファイルで選択を置換。上限超過時は元の選択を維持 |
| `FILEBOARDNAME パス\|表示名` | 表示名。80バイトまで |
| `FILEINFOEDIT パス` | 案内文を共通エディタで編集。8,192バイトまで |
| `FILEBOARDINFO パス` | 閲覧権限のあるボードの設定・表示名・案内 |

ルートのパスは `-` です。未設定の場合は管理者が先に `FILEBOARDSET -|member|member` などで作成してください。担当者は当該保管庫の閲覧権限も必要で、子ボードには担当権限を継承しません。掲示板SIGOPとは別設定です。担当者への今回の追加権限は登録者・属性・表示名・案内の変更であり、全体管理者の全操作を与えるものではありません。

登録者が属性を変更するには閲覧・登録権限が必要です。所有者変更・属性・案内の変更は保管庫の操作履歴に保存します。FILETYPEは取得回数を増やしません。文字コードの自動変換はせず、UTF-8として表示できない箇所を置換します。

### 転送方式

`PROTOCOL ZMODEM|XMODEM|YMODEM|YMODEM-G|MLINK|TEXT` で選択します。接続の既定はZMODEMです。

- `DOWNLOAD 番号`、`UPLOAD パス|説明`、`UPLOADBATCH パス|説明`、`BATCH SEND` が選択方式を使用します。最大16ファイル／合計256MiB。
- XMODEM／TEXTは単一ファイルのみ。アップロード時は `UPLOAD パス|ファイル名|バイト数|説明`。XMODEMは元の正確なサイズを入力し、末尾のパディングを検査して取り除きます。TEXTのサイズ0はCtrl-Zまで受信します。
- 既存の `ZUPLOAD`、`ZDOWNLOAD`、`ZRESUME`、メール添付転送は常にZMODEMです。途中再開は従来のZMODEMに限ります。
- XMODEM／YMODEMはlrzsz、YMODEM-g／M-LINK／TEXTは独立した転送エンジンです。同じ権限確認・一時保存・SHA-256確認・公開処理を通します。
- `FILEHISTORY` に方式を表示します。従来記録は `UNKNOWN`、専用LINEクライアントは `LINE` です。TEXTは受信側ACKを持たないため、送信側の書き出し完了を記録する方式です。

M-LINKは旧TRANSSYS.PASのM/header/CR/M/data/Eを参照しています。旧RTBBS実機端末との相互接続は未検証です。M-LINKとTEXTにはZMODEM相当の転送チェックサムがありません。失敗時に既に届いたファイルは取り消せません。転送中断後の再公開・回収は従来のspool管理に従い、別方式でZMODEM途中再開を呼び出さないでください。

## プロフィールと100グループ

`PROFILESET 項目 本文`、`PROFILEVIS 項目 PRIVATE|MEMBERS|MONTHDAY`、`PROFILEFIND 項目 検索語` を追加しました。

項目は `HANDLE BIO LOCATION OCCUPATION INTERESTS WEBSITE CONTACT MACHINE BIRTHDAY REALNAME KANA ADDRESS PHONE`。
追加の旧式項目（MACHINE以降）は既定で本人・全体管理者限定です。従来項目は従来どおり会員公開。HANDLEはチャット等の表示名にも使うため会員公開固定です。
`BIRTHDAY` は `YYYY-MM-DD`（1900〜9999年）、`MONTHDAY` は他会員には年を隠します。非公開内容と隠した年は他会員の検索にも使用しません。ゲストへプロフィール本文は公開しません。

`BIOEDIT` で最大8,192バイトの自己紹介を編集します。その他はHANDLE80バイト、各項目320バイトまで。従来のPROFILE／PROFILEID／PROFILESも継続利用できます。

グループは1〜100。`GROUP/UNGROUP 会員番号 グループ番号`、`PROFILEFIND GROUP 番号`、ボード権限 `g100` などに対応します。
`GROUPMOVE 元番号 先番号 CONFIRM` は全会員の所属を一括移行し、操作者以外の接続を終了して権限を更新します。ボードの権限指定そのものは自動書換しないため、必要に応じて別途変更してください。

## USERLOGと接続時間

- `USERTABLE [会員番号|ALL] [続き位置]` はログイン単位でログイン／終了UTC日時、秒数、累計ログイン回数、RAW/TELNETを一覧表示します。推測の回線速度は表示しません。従来USERLOGはイベント表示のままです。
- 通常は本人分だけ、全体管理者は全体を閲覧可能です。起動時の `--userlog-public` で他会員にも他会員分／全体の閲覧を許可します。ゲストは対象外です。
- `--userlog-keep N` は終了した直近N接続を保持（既定1,000、0は無制限）。接続中は全件保持。古い詳細を整理してもUSERSTATSの生涯ログイン回数・利用時間を保持します。整理に伴いログの位置番号は変わります。
- `--level-seconds 2:3600` のように0〜5の各レベルへ秒数を指定します。既定0は制限なし。接続受け付け時からの経過時間と現在の会員レベルを使い、ログアウト・再ログインで同じ接続の計時はリセットしません。
- `--time-warning 60` は終了前警告、`--time-grace 30` は制限到達後の猶予。転送中には警告文を混入させず、猶予後は切断します。
- 全体管理者は無入力制限・接続時間制限とも対象外です。レベル5やSIGOPだけでは免除しません。

## 共通エディタとチャット

- `EDITOR BETA|LINE`、`PAGER ON|OFF`、`PAGER SIZE 幅 行数`、言語を `PREFS SAVE` で会員設定へ保存。ログイン／`PREFS LOAD` で復元します。古い設定に画面サイズがなければ端末の通知値を保ちます。
- `BUFFER` は会員ごとの共通編集バッファ。`/SAVE` で保存、`BUFFER CLEAR` で消去。最大16,384バイト。
- 記事／メール等の編集中に `/BUFFER` で取り込み、`/TOBUFFER` で現在本文を共通バッファへ保存します。投稿・メール下書きとは別です。取り込みにはUndoが使えます。
- 編集中の `/ONLINE` または `/W`、`/STATUS` または `/#`、`/TALK 接続番号` または `/T`、`/MEMO 本文`、`/CALL` を追加。個別トークを `//` で閉じると元の編集に戻ります。ラインエディタのコマンド待ちではW/T/#も使えます。
- チャット中の `D` はSYSOP呼び出し、`#` は接続状態、バックスラッシュまたはバッククォート2個は接続者メモ入力です。

## 更新とバックアップ

停止した状態で記事・会員・付随ファイル・保管庫・設定を一式バックアップしてから更新してください。stage60の会員・保管庫は読み込めますが、追加機能の保存後に古いruntimeへ戻す場合は更新前の一式へ戻してください。

`members.dat.profile` がプロフィールの公開範囲・長文・共通バッファを保持します。会員本体へ使用開始印を残し、使用開始後にこのファイルが欠落／不完全なら起動を拒否します。非公開設定を失って従来の公開情報へ戻ることを防ぎます。
`*.userlog.lock` は整理中も二重起動を防ぐロックです。稼働中に取り除かないでください。ログ整理用ツールでプロフィール・会員・保管庫の状態ジャーナルを削減しないでください。

試験結果は `evidence/stage61/VERIFICATION.md` を参照してください。
