# セキュリティと権限モデル

本体は専用の非rootアカウントで動かします。管理者会員はOSのrootではありません。DoorはBBSと同じOSユーザーで動くため、信頼できるプログラムだけを登録し、root runtimeからの起動は拒否します。PTYはゲームのOS隔離機構ではありません。

Telnetは平文です。インターネットからの利用にはHTTPS WebUIまたは適切に管理したSSH経路も検討します。低番号ポートを使うためだけに本体をrootで常駐させず、[ポート23の権限](connections.md)を設定します。

会員認証、管理者・SIGOP、レベル・グループ、親ボードACL、共通パスワード、本人所有、メール送受信者を各操作で確認します。Web画面の非表示だけに依存しません。パスワード・会員データ・秘密鍵をWeb公開ディレクトリへ置かないでください。

Webアカウントは私有spoolと設定を必要範囲で読み書きしますが、BBS原本を直接読みません。Cookie/CSRF/Originを検査し、任意のOSコマンド・SQL実行画面はありません。Webリンク管理は限定されたローカルソケットで、秘密鍵・DBへの読取権を与えません。

共有リンクは接続相手と発信元を別に登録し、公開鍵の指紋を別経路で照合します。会員・私信・保管庫は共有しません。識別鍵を失うと同じBBSとして復旧できなくなるため、専用DBと合わせて保存します。

バックアップは会員・私信・認証情報を含み、暗号化ファイルではありません。所有者だけが読める場所へ保存し、第三者提供や公開ZIPへ混入させません。手順の設定例に実際のパスワード・秘密鍵を書き込まないでください。

SELinuxが有効な環境で動作することと、専用の制約ドメインを提供することは異なります。0.74.1 RPMは専用SELinuxポリシーを提供しません。拒否をSELinux無効化で回避せず、ラベル・サービス権限・パスと拒否ログを確認します。

## 次期版のSELinuxポリシー（検証中）

`linux/selinux/` にRocky Linux 10向けの任意導入パッケージ
`liner-notes-selinux` を用意しています。既存の本体・Web・リンクの3 RPMとは別です。
コンパイル成功とEnforcingでの機能・拒否試験は異なるため、後者の確認完了までは
本番向けの全機能対応として扱いません。0.74.1の配布物は変更しません。

|対象|専用ドメイン|主な保存領域|
|---|---|---|
|BBS|`liner_notes_t`|`/var/lib/liner-notes`|
|WebUI|`liner_notes_web_t`|`/var/lib/liner-notes-web`|
|リンク|`liner_notes_link_t`|`/var/lib/liner-notes-link`|
|管理コマンド|`liner_notes_admin_t`|オフライン管理対象|
|Door|`liner_notes_door_t`|`/var/lib/liner-notes-doors`|
|転送helper|`liner_notes_transfer_t`|`/var/lib/liner-notes/spool`|

オフライン管理用の保存先 `/var/backups/liner-notes` は `liner_notes_backup_t` です。
管理者が適切なUnix所有者・権限で作成してから明示的にラベルを設定します。
通常のデーモンはこの保存先を読みません。復元したコピーを運用データとして使う際は、
停止中に配置先・所有者を確認し、BBS用のラベルへ復旧してから起動します。

WebUIからBBS原本やリンク秘密鍵への直接アクセス、DoorからBBS原本へのアクセスは許可しません。
転送helperは既存の認証済みRPC用FDを使用します。Doorへの切替には管理者が設定した
`liner_notes_door_exec_t` の実行ファイルが必要です。任意の外部ゲームを無条件に許可する仕様ではありません。
Doorの外向き通信は既定で無効です。必要な場合だけ `liner_notes_door_network` と接続先ポートを検討します。

ポリシーRPMは優先度200で導入し、優先度400のローカルポリシーを置き換えません。
導入・更新ではサービスを再起動せず、データや設定を再ラベルせず、ポート定義も変更しません。
稼働中の専用ドメインがある場合は削除を拒否します。削除時も管理者の
`semanage fcontext` / `semanage port` の設定と保存データを消しません。

`liner-notes-selinux` は既定で現在のモードとモジュール登録を表示するだけです。
明示的な `liner-notes-selinux --relabel` はroot専用で、標準配置の存在するパスだけに
`restorecon -R` を実行します。ローカルなfcontext定義を尊重し、`-F` は使用しません。
独自配置は管理者が対象パスと適切な型を確認して登録してください。ポートの既存割当は
`semanage port -l` で確認し、他のサービス用の型を機械的に変更しないでください。

### Enforcing受入CIの準備（検証機専用）

`Rocky SELinux policy` のPRジョブはポリシーのコンパイル、許可ルール、RPMの導入・
更新・削除とローカルラベル保持を検査します。mainジョブは既存CI専用鍵でRocky検証機へ
接続し、Telnet、記事、メール、SQLite再起動、Web、署名付きリンク、Door、実ZMODEMと
管理コマンドを専用ドメインで試験します。Web・Door・転送から原本を読めないことは、
同じUnix所有者のファイルを用いてMAC拒否として確認します。
リンク試験では双方から相互TLSでpullを行い、署名付き記事の受信とBBS記事の外部への送信をそれぞれ確認します。
同じZMODEM送受信手順をhosted CIでも実BBS・実lrzszで確認します。これは転送手順の回帰検査であり、専用ドメインとMAC拒否はEnforcing実機で別途確認します。
管理ドメインで論理バックアップ・内容検証・別コピーへの復元を実行し、復元したコピーの
ラベルをBBS用に設定したうえで記事とメールを再度読み出します。

実機ジョブには、検証機の管理者による次の準備が必要です。これは本番導入手順ではありません。
通常のCIアカウントへ任意のsudo権限を与えず、レビュー・統合済みmainにある
`ci/selinux_root.py` だけをroot所有の `/usr/local/libexec/liner-notes-ci-selinux` として
配置します。CIは配置したファイルのSHA-256が対象コミットと一致することを検査します。

```sh
# 10.0.0.51 の管理者が、レビュー済みmainのソースディレクトリで実行
dnf install selinux-policy-devel policycoreutils-python-utils gcc make \
    openssl-devel sqlite-devel python3-aiohttp python3-cryptography
# 実転送用のlrz/lszは、下記のLinux導入手順に従って別途用意する。
# 未作成の場合だけ追加。CI SSHアカウントとUID・グループを分離する。
useradd --system --user-group --no-create-home --home-dir /nonexistent --shell /sbin/nologin liner_notes_ci
install -d -o root -g root -m 755 /usr/local/libexec
install -o root -g root -m 755 ci/selinux_root.py /usr/local/libexec/liner-notes-ci-selinux
visudo -f /etc/sudoers.d/liner-notes-selinux-ci
```

sudoersには固定ヘルパーのみを許可します。受け付ける引数は40文字のコミットSHAだけで、
ヘルパー自身もGitHub上の最新mainと二度照合します。ファイルパス、URL、任意コマンドは
受け付けません。環境変数も固定し、SSHユーザーが書けるチェックアウトは実行しません。
試験用サービスは専用の `liner_notes_ci` で動かし、SSHの `ci-test` には試験データの
書き込み権を与えません。rootが準備中のパスを呼出元ユーザーに置き換えられることを防ぎます。

```text
ci-test ALL=(root) NOPASSWD: /usr/local/libexec/liner-notes-ci-selinux
```

ヘルパーは検証機のアドレスとOS、Enforcingを確認し、root専用領域に取得したmainから
固定のビルドと試験を行います。既存の `liner_notes` ポリシーがあれば上書きせず停止します。
試験中だけ優先度200でポリシーを導入し、新規のループバック待受・一時systemdユニット・
専用データ領域を使用します。既存サービス、会員データ、ローカルラベル定義は変更しません。
終了後は当該ユニットと追加したポート定義を除去してポリシーを外します。プロセスが残った
場合はポリシーを残して失敗とし、root専用の記録と作業領域を保全します。準備不足や試験失敗を
スキップ成功にはしません。

タイムアウトやrunner切断時にも一時サービスは最長300秒で停止しますが、ポリシーが
残った状態で次の試験を始めることはできません。検証機の管理者はroot専用作業領域の
`fixture-state.json` と `acceptance.log` を確認し、記録された一時ユニットの停止・
当該ポート定義の除去・専用ドメインの残留がないことを確認してから、試験用の優先度200の
ポリシーを除去します。既存ポリシーを無条件に削除する回復コマンドは用意しません。

試験終了時は、監査ログとkernel journalから専用ドメインに対する直近のAVC拒否だけをCIログへ記録します。
無関係なサービスの拒否や、PATH・EXECVE等の監査レコード全体は公開しません。
拒否ログを無条件に許可ルールへ変換せず、操作・保存先・必要な権限を確認してください。

配布元のlrzszがない場合や必要な転送試験に失敗する場合は、[Linux導入](install-linux.md)の
隔離ビルド手順で用意した実エンジンを使います。版名だけで転送成功を判断しません。
この実機CIの成功までは、専用SELinuxポリシーは検証中です。
