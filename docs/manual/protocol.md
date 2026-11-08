# 通信とコネクタの技術仕様

## 接続状態

一般端末はTelnet交渉を扱うクライアントで `--telnet --menu` の待受へ接続します。RAWは行単位の機械向け応答です。各コネクタも通常のBBS会員認証・権限確認を通り、OS側のサービスユーザーとBBS会員を混同しません。

Webは `CONNECTOR OPEN`、ランダム要求番号付き `CONNECTOR SYNC` で応答終端・画面状態を同期します。`CONNECTOR LITERAL` で本文の `.` や `/CANCEL` を命令として誤認させず、`CONNECTOR BLOCK/DATA` で大きいブロックを分割します。同期トークン自体は認証情報ではありません。

## ファイル転送API

line-v1はテキストコマンド、binary-v1は接続上のバイナリ受け渡しです。予約・チャンク・確定・状態照会・取消を区別し、オフセット、サイズ、SHA-256、所有会員、名前空間、登録先権限を検査します。公開保管庫と非公開メール添付の命令は分離します。旧FILEPUT/FILEGETの小さいチャンク上限を通常入力上限と混同しません。

Web対応コネクタは64KiB単位の転送を用います。更新結果が不明な場合は同じ予約を照合し、完了済みを重複登録しません。受信完了と登録完了、送信完了と利用者の保存完了は異なります。各方式の成功は個別完了通知で確定します。

独自クライアントを実装する場合、[0.74.1のプロトコル実装](https://github.com/matsuotakeshi/liner-notes/tree/v0.74.1/src)と同版のテストを使用します。以下の詳細リファレンスに従い、成功応答を推測せず、エラー時にオフセットを照合してください。

## line-v1の要求と応答

RAW表示で使用します。RAWでもTelnet制御は維持します。

| 要求 | 応答 |
|---|---|
| FILEMETA id | FILE id size sha256、END FILE |
| FILEGET id offset | FILEDATA id offset count base64 |
| FILESTART size sha256 name64 description64 | UPLOAD id |
| FILESTARTIN size sha256 name64 description64 category64 | UPLOAD id |
| FILEPUT id offset base64 | RECEIVED id next_offset |
| FILESTATUS id | UPLOADSTATUS id ACTIVE/COMMITTED/CANCELLED received size sha256 |
| FILEFINISH id | UPLOADED id size sha256 |
| FILECANCEL id | UPLOAD CANCELLED id |
| FILECHECK category64 | FILEBOARD READY |

名前（100バイトまで）・説明（240バイトまで）・分類（48バイトまで）はUTF-8のBase64、空は `-`。ハッシュは小文字16進64桁です。通常FILEPUTは672バイト、FILEGETは最大768バイト。現在の通常入力行上限4096バイトへ拡大しても、この互換チャンク値は変更しません。FILEPUTBLOCK/FILEGETBLOCKは最大65536バイトで、長い要求はコネクタのBLOCK/DATA経路を使います。

主な拒否応答は `ERR vault denied` / `ERR no file` / `ERR vault busy` / `ERR vault limit` / `ERR vault storage` / `ERR vault syntax`。予約操作ではその時点の認証と所有者を確認します。一般会員は自分の予約だけを扱い、管理者の状態照会・取消権限と本人の書込・確定権限を区別します。

## binary-v1

認証後に `TRANSFER BINARY` を送り、`BINARY READY binary-v1` の後にフレームを送ります。Telnetは双方向BINARY交渉が必要でIACのエスケープを維持します。転送中はメニュー・エコー・通常命令を混入させません。

フレームは `~` 1バイト、ペイロード長4バイト（big endian）、ペイロード。要求長1〜1024、応答長最大2048バイト。IDとオフセットも4バイトbig endianです。

| 種別 | 内容 |
|---|---|
| L | FILEMETA/START/STATUS/FINISH/CANCEL等の改行なし行要求 |
| R | ファイルID+オフセット、最大768バイト取得 |
| W | 予約ID+オフセット+1〜768バイト書込 |
| Q | 正常終了。E応答後にBINARY CLOSEDと元画面 |
| T応答 | 通常の転送応答文字列 |
| D応答 | ファイルID+オフセット+0〜768バイト |

1要求ずつ応答を待ちます。不正フレーム・未対応種別・通常BBS命令混入は切断。途中フレームは15秒、モード待機は60秒で切断します。これは外部lrzsz helperの無通信120秒とは別です。会員停止・認証失効時も通常画面の文字を混ぜず切断します。

## Python送受信クライアント

```sh
python3 scripts/vault-upload.py --connector binary-v1 --host 127.0.0.1 --port 2222 --user bob --state upload.json sample.zip
python3 scripts/vault-upload.py --connector binary-v1 --host 127.0.0.1 --port 2222 --user bob --state upload.json --resume sample.zip
python3 scripts/vault-upload.py --connector binary-v1 --host 127.0.0.1 --port 2222 --user bob --state upload.json --cancel
python3 scripts/vault-download.py --connector binary-v1 --host 127.0.0.1 --port 2222 --user bob 1 received.zip
```

省略時はline-v1。scripts一式が必要です。パスワードは対話入力し、引数・stateには保存しません。stateは接続先・会員・コネクタ・元ファイルパス・名前・説明・サイズ・ハッシュ・予約状態を持つ私有ファイルです。初回に既存stateを上書きしません。受信先も未作成パスです。

再開は元ファイル全体と接続先・会員・サーバー状態を照合します。切断時に自動再接続・取消をせず、開始応答を失ってID不明の場合も重複予約しません。確定後もstateを保持し、公開済みの再確定は別IDを作りません。確定後のcancelは拒否します。

## 保存とワーカーの境界

TransferBackendのinfo/read/start/write/status/finish/cancelを、line/binaryの符号化から分離します。保管庫ワーカーは会員DBを直接開かず、会員ワーカーが解決した主体と認証世代を使います。遅延応答を再利用済み接続枠へ送らず、確定直前にも認可を再確認します。ただし既に保存に入った処理は切断と同時でも完了する場合があるため、結果不明では状態照会を使います。

未公開予約は一覧に出ません。容量予約はOSのディスク事前割当ではなく、空き容量不足では失敗します。実体は内部IDから作り、利用者指定名を保存先パスにしません。確定にはサイズ・SHA-256・同期を確認し、未公開一時ファイルと公開記録を区別します。ローカル保管庫はハードリンク対応のファイルシステムが必要です。同時予約は全体24件、1主体2件で、完了・取消で枠を解放します。
