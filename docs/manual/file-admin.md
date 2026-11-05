# ファイルボードと登録済みファイルの管理

## ボード設定

```text
FILEBOARDSET パス|閲覧ACL|登録ACL
FILEBOARDNAME パス|表示名
FILEINFOEDIT パス
FILEBOARDSIGOP パス|会員番号
FILEBOARDSIGOP パス|NONE
FILEBOARDPASS パス
FILEBOARDPASS パス NONE CONFIRM
FILEBOARDOPTIONS パス|BOARDまたはMENU 表示順 取得履歴保持件数
```

ルートのパスは `-`。階層の親から閲覧条件・パスワード・SIGOP権限を適用します。MENUは振り分け専用で登録できません。取得履歴保持0は無制限です。履歴を削っても累計取得回数は保持します。

## ファイル管理

| 命令 | 用途 |
|---|---|
| FILEOWNER 番号 会員番号 | 登録者変更 |
| FILERENAME 番号 名前 | 登録名変更。IDと実体は維持 |
| FILEDESC 番号 説明 | 説明変更 |
| FILEASCII 番号 ON/OFF | 端末で直接表示するテキスト属性 |
| FILECATEGORY 番号 パス | 分類変更 |
| FILEHIDE 番号 CONFIRM | 公開を停止 |
| FILERESTORE 番号 CONFIRM | 再公開 |
| FILEDELETE 番号 CONFIRM | 削除・本体回収。復元不可 |
| FILECOPY 番号 宛先 / BATCH COPY 宛先 | 担当範囲間コピー |

本人の説明変更・削除には閲覧・登録権限が必要です。管理コピーは元と先の両方の管理権限を確認します。登録者・説明・日付・ASCIIを保持し、新IDを発行して取得回数は0から始めます。新しいファイルとして容量・件数を消費します。

`FILEAUDIT` は変更履歴、`FILEHISTORY` は取得履歴、`FILESTATS` は完了確認済み取得回数です。履歴の転送方式は実際の登録・転送記録に基づき、古い未記録分を推測して補完しません。

中断受信は公開ファイルの削除とは別です。[中断データの管理](transfer-admin.md)を使います。私信添付の取得権限が保管庫SIGOPによって広がることはありません。
