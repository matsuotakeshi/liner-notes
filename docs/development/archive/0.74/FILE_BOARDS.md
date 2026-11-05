> この文書は機能追加時点の記録です。現行の導入入口は [README](README.md)、今回の変更は [STAGE65](STAGE65.md) を参照してください。古い段階名・上限・未実装事項は当時のものです。

# ファイルボードの設定と操作（stage54）

## 管理者の設定

保管庫の `index.dat` に権限設定を記録します。runtimeの会員管理と別の保存領域です。管理はサーバー上の `vaultctl` で行います。例の `vault` は実際の保管庫ディレクトリへ置き換えてください。

```sh
./vaultctl board-set vault - member member
./vaultctl board-set vault software member none
./vaultctl board-set vault software/freebsd l3+g2 l4+g2
./vaultctl boards vault
```

`-` は未分類／ルートです。親を先に作ります。閲覧には対象と全ての親の閲覧条件、登録にはそれに加えて対象ボードの登録条件が必要です。親の登録不可は子の登録には継承しません。

条件は `member` または `all`（いずれも認証会員）、`none`、`l0`～`l5`（最低レベル）、`g1`～`g32`（所属グループ）。`l3+g2` や `g1+g2` は全条件を満たす必要があります。SYSOPは条件を迂回できますが、会員レベル5だけでは迂回できません。

ボード数はルートを含め128。パスはUTF-8で48バイトまでで、大小文字を区別します。日本語・空白も使用できます。空の区切り、`.`、`..`、末尾の `/`、`|`、バックスラッシュ、コロン、制御文字は使えません。削除・名前変更コマンドはありません。利用停止は権限を `none` に変更してください。

### 既存保管庫への導入

設定前は従来どおりです。最初のルート設定でボード制御を有効にすると、未定義カテゴリは一般会員から見えなくなります。**停止中の保管庫をバックアップし、ルートから順に、既存カテゴリも設定してください。** パス条件に合わない旧カテゴリは管理者が登録済みボードへ移します。旧カテゴリを自動的に公開扱いにはしません。

設定変更は記録後の次の操作から反映されます。会員レベル・グループ変更は従来どおり既存セッションを失効させます。権限を取り消されたアップロードの取り消しは管理者が `vaultctl upload-cancel` で行えます。

新しいボード設定・カテゴリ指定登録の記録はstage53以前では読めません。旧版へ戻す場合はバックアップ一式へ戻してください。BBS記事・会員ファイルの形式は今回変更していません。

## 利用者の操作

```text
FILEBOARDS
FILESIN software
ZUPLOADIN software/freebsd|FreeBSD用資料
```

保管庫メニュー内では `B`、`I software`、`UIN software/freebsd|FreeBSD用資料`。説明を付けない場合は `UIN software/freebsd`。空白を含むパスも使えます。`|` より後は説明で、UTF-8で240バイトまで。従来の `U [説明]` は未分類へ登録します。

一覧は8件ずつ、`+` で続き。`FILECATEGORIES` はファイルが存在するカテゴリと件数、`FILEBOARDS` は空のボードも表示します。表示される登録可否はその時点の権限です。`FILESIN -` は権限設定後はルート配下全体、未設定時は従来どおり未分類のみです。

専用クライアント:

```sh
python3 scripts/vault-upload.py --host HOST --port 2222 --user USER --state upload.json --category software/freebsd file.zip
python3 scripts/vault-upload.py --host HOST --port 2222 --user USER --state upload.json --resume file.zip
```

line-v1／binary-v1双方で登録先を指定できます。未対応の別コネクタにはカテゴリ指定を黙って無視させません。公開に失敗したZMODEM受信は従来どおり非公開の回復ディレクトリを保持し、`upload.json` に登録先も記録します。

管理者が既存ファイルを別ボードへ移動する操作は `FILECATEGORY 番号 パス`。管理者の直接取り込みは `vaultctl import-in ROOT CATEGORY SOURCE NAME DESCRIPTION`。予約段階から登録先を記録し、公開直後に誰でも読める未分類へ一瞬置く処理はありません。

## コネクタと保存形式

認証情報は `Principal.level` と `Principal.groups` で渡し、保管庫から会員データベースへは依存しません。`vault_upload_start_in` が転送方式によらず登録先を受け取ります。

line-v1の `FILESTARTIN size sha256 name64 description64 category64` は各文字列をbase64で指定（空文字列は `-`）。`FILECHECK category64` は現在の登録権限を検査します。ZMODEMヘルパーもこのAPIを使います。権限チェック済みでも、各チャンク・最終公開時に再検査します。

保管庫ジャーナルにB（ボード設定）、a（カテゴリ付き直接取り込み）、r（カテゴリ付き予約）を追加。従来レコードはそのまま読み、新形式は長さ・SHA-256・パス構文・親の存在を検証します。公開と登録先を別々に更新しません。設定変更は管理履歴にも記録されます。

## 今回の対象外

メール添付、一括アップロード、ZMODEM自体の途中再開、旧RTBBSのエディタ切替は未実装です。通常クライアントの保存状態からのアップロード再開と、ZMODEM回線上の途中再開は別の機能です。
