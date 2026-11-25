# ネイティブCI並列実行

現行運用方針は [CI_CD.md](../../../CI_CD.md)。利用者向け機能や本番配備の変更ではありません。

## 構成

以前は3OSのActionsジョブを別々に定義していても、self-hosted Runnerが1枠のため、3台の試験を順に実行していました。現在は `Multi-OS Native CI` の1ジョブが `ci/native_ci.py --target all` を呼び、3つのSSH試験を並列実行します。

| 対象 | 接続先 | 既存鍵 |
| --- | --- | --- |
| FreeBSD | takeshi@10.0.0.50 | /home/ci/.ssh/id_ed25519_freebsd_ci |
| Rocky Linux | ci-test@10.0.0.51 | /home/ci/.ssh/id_ed25519_rocky_ci |
| Debian | ci-test@10.0.0.59 | /home/ci/.ssh/id_ed25519_debian_ci |

DebianのSSHホスト鍵を親機の `ci` ユーザーの `known_hosts` に登録し、専用公開鍵をDebianの `ci-test` に登録します。Solarisは独立したmain限定のSolaris Native CIで検証し、この3OSゲートへ追加していません。pkg/RPM/DEB生成は従来の独立ワークフローのままです。

## 信頼境界と衝突防止

- self-hostedジョブの入口は本リポジトリのmainに限定します。実行処理もref、イベント、リポジトリ、チェックアウトSHAを照合してからSSHへ進みます。PRはGitHub-hostedの合成試験のみです。
- コミットを `git archive` で固定し、全OSへ同じアーカイブを渡します。リモートでSHA-256を照合し、絶対パス・上位参照・リンク・特殊ファイルを拒否して新規ディレクトリへ展開します。
- 作業先は各CIアカウントの `~/ci/native-OS-ランダム値/source`。既存の `~/ci/liner-notes` を削除・上書きしません。
- Solarisだけは検出済みのPython 3.9以降の `tempfile.mkdtemp` を使用します。OSの `mktemp` が生成するピリオド入り接尾辞を避け、既存の転送先・SMFパス検査を維持します。ディレクトリは排他的に作成し、権限0700、umask077とします。他OSは従来のmktempを使います。原因と検証範囲は[調査記録](../solaris/TEMP_DIRECTORY.md)を参照してください。
- 単体OSの手動ワークフローも `ci/native_ci.py --target OS` を利用します。すべて同じActions concurrency group `liner-notes-native-hosts` を使い、ホスト側でも `~/ci/.liner-notes-native.lock` を排他取得します。
- `StrictHostKeyChecking=yes`、既存の専用鍵・アカウントを維持し、パスワード認証へフォールバックしません。

## 試験・失敗時の動作

FreeBSDは `make -j2` と `make check`。Linuxは従来どおり `CC=clang PYTHON=python3 SQLITE_CFLAGS= SQLITE_LIBS=-lsqlite3` を指定してビルドと `check` を実行します。`LINER_TEST_START_TIMEOUT=180` も維持します。

1OSの失敗で他OSの結果収集を打ち切りません。最終的に対象OSすべての成功と同一SHAの結果通知を確認し、失敗・欠落・中止・不一致を成功扱いしません。最終ゲート名 `All required native suites passed` とワークフロー名 `Multi-OS Native CI` は維持し、リリース前検査から引き続き参照できます。

Runnerは5秒ごとに生存通知を送ります。リモートはEOF、45秒の通知途絶、シグナル、全体55分のタイムアウトでビルドのプロセスグループを停止します。Runner中止時も入力を閉じて終了を待ちます。回線断で終了通知が届かない場合は、リモートの生存監視が停止を行います。これはCIの子プロセス管理であり、本番サービスや別セッションの管理ではありません。

## ログと所要時間の確認

ActionsログにOS別の開始・完了を表示し、Job summaryへ結果と所要時間をまとめます。`native-ci-results` Artifact（単体実行は `native-ci-OS-results`）には次を30日間保存します。

- `freebsd.log` / `rocky.log` / `debian.log`: OS別のビルド・回帰試験出力。
- `results.json`: source SHA、archive SHA-256、OS別結果、エラー、リモート作業先、開始/終了日時、合計所要時間。

`tests_started_at` と `tests_finished_at` はすべてRunner時計で記録します。3OSの開始時刻の最大値が終了時刻の最小値より早ければ、実機試験が同時進行したことを確認できます。合計時間を以前の直列実行と比較します。試験時間は環境負荷で変わるため、速度を固定値で保証しません。

障害調査用にリモートの専用ディレクトリは残します。Artifactと必要な証跡を確保した後、運用者が対象runの作業先を確認して整理してください。他プロジェクトを含む競合時は最大20分待機し、待機中も接続断・中止・heartbeat途絶を監視します。待機時間を既存の全体タイムアウトへ含めます。期限超過は別試験が稼働中である可能性を示します。ロックファイルの削除や他の試験の強制終了で回避せず、実行履歴を確認します。

## 検証

`Native CI Source Checks` はGitHub-hosted Runnerで `tests/test_native_ci.py` を実行します。並列開始、1台失敗時の継続、結果の欠落/重複/SHA不一致、転送アーカイブの検証、main限定、プロセス終了、接続断、通知途絶、タイムアウトをLANアクセスなしで確認します。

main統合後の実機CIで全3OSの成功と実行区間の重なりを確認します。合成試験の成功だけを実機並列動作の証明とは扱いません。ワークフロー実装や検証証跡の更新は、この開発文書とCHANGELOGへ反映します。
## 負荷時の認証・終了待機

ネイティブworkerは試験用環境変数 `LINER_TEST_AUTH_TIMEOUT=30` と
`LINER_TEST_STOP_TIMEOUT=30` を渡します。認証応答待ちは通常の10秒から30秒、
共通Serverハーネスの終了待ちは通常の5秒から30秒になります。これらはテスト
専用で、製品の認証・無入力・転送タイムアウトは変更しません。起動待機180秒、
suite全体の期限、heartbeat、ホスト排他、main限定条件も従来どおりです。

この2値は有限の正数かつ120秒以下に限定します。期限超過は引き続き失敗し、
終了時のプロセス状態・RSS・終了コード・stderrの診断を保持します。
認証ヘルパーが設定したsocket待機時間は従来どおり後続操作にも残りますが、
各テストの明示的なsettimeoutは優先されます。性能上限・RSS上限・機能の期待値は
変更しません。明示的な短時間制約を検証するテストの期限も変更しません。

変更の契機はmain `1a22f945eac705329641a70faa4b09254a4669d7` の
[native run 43921168697](https://github.com/matsuotakeshi/liner-notes/actions/runs/43921168697)
で、Rockyのtest_visibilityが会員登録応答待ちと終了待ちを超過し、終了時に
`Dl`状態が記録されたことです。Artifact IDは14256880604です。I/O停滞は仮説であり、
この待機猶予だけで根本原因の解消やLN-30の完了とは扱いません。

ローカルの診断テスト14件、native制御テスト11件は成功。Windowsでは既存の
POSIX専用5件は対象外であり、既存hosted CIで実行します。main統合前に自宅VMへ
未審査コードを渡しません。今回のCI設定変更は公開BBS機能の説明には影響しません。
