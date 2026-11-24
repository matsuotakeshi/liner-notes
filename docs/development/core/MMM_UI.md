# MMM風端末UIの開発版

設計の基準は [Issue #54](https://github.com/matsuotakeshi/liner-notes/issues/54) です。階層メニュー、区切り文字、読書とライン編集の操作は [mmm Revision 4.0 ユーザーマニュアル](https://webmid.kinet.ne.jp/mid/manual/wtsbbs/manual/MASHMAN/USER-utf8.txt) のコマンド、エディタ、ノートの各章を参照しました。MASHの操作は別プロファイルの調査対象です。

## 有効化

会員データのパスを指定して起動します。既存の既定値は `FIXED` と `rtbbs` です。

```sh
./runtime --telnet --menu --ui-policy MEMBERS --ui-default rtbbs --ui-allow rtbbs,mmm 2222 notes.dat members.dat
```

上の設定ではログイン後の主メニューで `UI mmm` を入力します。最初からMMM風画面にするには `--ui-default mmm` を指定します。MMM風画面では記事の閲覧を含め、会員としてログインしてから機能を利用します。一時利用者に公開する場合は、管理者がアクセス範囲を定めた会員アカウントを用意できます。会員登録は局の既存の登録・審査設定に従います。

## 操作

| 場所 | 操作 |
| --- | --- |
| 接続直後 | `login ID`、局が許可する場合は `register ID` |
| 主メニュー | `1` メール、`2` ノート、`3` 保管庫、`4` チャット、`5` 設定、`6` ゲーム、`7` 終了。 `/` で主メニュー、 `.` で一つ上の階層 |
| ボード | 番号で選択、 `L` で続き。通常ボードでは `1` で索引、 `2` で投稿、 `3` で子ボード |
| ボードの直接指定 | `go #番号`、 `open [-i|-s|-x|-b番号|-n [-a]] [ボード]`、 `post [ボード]`。ボードは番号、`@ラベル`、一意な表示名で指定可能。 `-s` は新着がなければ元の画面に戻り、`-x` は索引を表示 |
| 索引・読書 | 画面に表示された番号や単独キーで操作。空白・Backspaceで移動、 `w` で投稿または返信、 `i` で索引、 `q` で読書終了 |
| 読書中の検索 | `f/F` はベースノートの題名検索と続行、 `'` と `"` は返信の題名検索と続行。 `l/L` は共通の既読状態を使った未読移動 |
| エディタ | `.` で本文入力を終えて編集へ。 `l/p` で表示、 `a/u/i` で追加・挿入、 `d` で削除、 `r` で文字列置換。 `w` で保存、 `q` で破棄 |
| メール | `find 語句`、`news YYYY-MM-DD`、`notify [on|off]`、`killmail 番号 confirm`、`save 番号`。`inbox`、`sent`、`unread`、`more`、`read ID`、`send USER`、`reply ID`、`delete ID confirm`。編集中に `s` で下書き保存、`attach ID` と `detach ID` で添付を選択。メール画面の `resume` で下書きを開き、`files` で添付用ファイルを確認。ZMODEMの `upload`、`uploadbatch`、`resumebatch`、`get ID`、`getall メール番号` |
| 保管庫 | `cd パス`、`dirs`、`list`、`more`、`find *.TXT`、`info ID`、`text ID`、`unlock [パス]`。ZMODEMの `get ID`、`put [説明]`、`resume`、`putbatch`、`add ID`、`batch`、`clear`。`protocol [方式]` から他の転送方式を選択できます。 |
| チャット | 行入力で発言。 `/who` で接続一覧、`/wire ID 本文` で個別に送信、`/leave` で戻る |
| 設定 | `lang ja|en`、`size 列 行`、`save`、`load`、`profile [USER]`、`set FIELD VALUE` |
| ゲーム | 許可されたゲームを一覧し、番号または `play ID` で起動。終了時はゲーム一覧に復帰 |
| 新着 | `new`、`new -n`、`setsequencer [YYYY-MM-DD|reset]`。巡回中は一覧の番号で本文を表示し、`more` で続く。 `scanlist set #3 @synth` で最大64件を順序付きで保存、`scanlist -d` で既定へ戻す |

通常コマンドは大小文字を区別せず、一意なら省略できます。読書中の単独キーは大小文字を区別します。索引の数字は表示行の番号です。記事IDを直接指定するには `b` または `open -b番号` を使います。返信の表示番号を入力する場合は、最初の数字の入力後に残りを入力してEnterで確定します。エディタの範囲は `1,3`、 `2,$` のように指定し、削除と置換では `!` で範囲外を選べます。`r1,$ OLD NEW` は指定範囲の文字通りの文字列を置換します。

## Coreとの境界

- `session_mmm.c` は画面遷移と編集・ノート読書、`session_mmm_services.c` は共通サービスとの接続を扱います。`mmm_input.c` が主メニューのコマンドと編集範囲を解析します。
- ボード選択・投稿準備・実際の書き込み・一覧と本文の取得に既存の権限判定を適用します。読み取り用に追加した型付き要求はボード階層とノートの順序・題名検索を返します。非表示記事や閲覧権限のない記事はその候補から除外します。
- 既存のWebUI等は従来のRAWハンドシェイクを使います。MMM既定の待受からRAWに入った接続にもCoreのログイン条件を適用し、その接続でのUI切替を禁止します。
- MMMの新着基準は `news` のMMM専用種別に保存します。`new`（`-n` で連続表示）または `open -s/-x` で本文を表示した後の `off` がログイン時刻を自動保存します。`setsequencer` の上書きは接続中だけ使います。同じ秒の投稿を飛ばさないよう境界秒を含めます。会員による明示的な時刻設定では `TZ` の日付を使います。
- `mmm_scanlist` は会員・順番・ボード番号、`mmm_board_labels` はボード番号とラベルの対応を保存します。既存DBには表を追加し、論理バックアップと持ち出しに含めます。ACL、入室パスワード、削除状態をクエリごとに検査します。
- 端末統合試験は `make test-mmm-ui test-mmm-services test-mmm-traversal test-mmm-input` で実行できます。転送試験ではテスト用ヘルパーで引数と完了応答を検証します。既存の実ZMODEMとSSHの統合試験にもMMMの扱いを含めています。

旧mmmの操作仕様は Revision 4.0 ユーザーマニュアルの `open`（「連続表示オプション」）、`new`、`scanlist`、`setsequencer`、`killmail`、`pfile` を参照しました。`-n` は連続表示です。Liner Notesの最大64ボード制限とボードACLも適用されます。パターンと除外指定は `mmm_scan_rules` に保存し、既存の巡回表と同じトランザクションに含めます。巡回中の展開結果にもACLを適用します。

署名は `mmm_signatures` に保存し、自動付加を有効にすると返信とメールの確定時に共通Coreが適用します。

未対応の範囲は旧版のライン会議、パーソナルファイルへの直接バイナリ受信です。MASHは別のUIプロファイルです。
