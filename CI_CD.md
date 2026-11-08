# Liner Notes CI/CD・本番リリース運用設計

> 状態（2026-11-08）: **3OSのCI候補生成・同一main SHAの照合・Debianの隔離パッケージリビジョン更新試験は成功**。本番リリース前の読み取り専用ゲートを強化中です。本番更新・自動ロールバックは未実装であり、OS別実更新・バックアップ/復元・リンク互換性確認までデプロイを有効にしないこと。

## 検証環境と本番環境

| 場所 | ホスト | 役割 |
| --- | --- | --- |
| 自宅 | 10.0.0.54 / Ubuntu | GitHub Actions self-hosted Runner / CI管理 |
| 自宅 | 10.0.0.50 / FreeBSD | FreeBSD Build and Test |
| 自宅 | 10.0.0.51 / Rocky Linux | Rocky Linux Build and Test |
| 自宅 | 10.0.0.52 / Solaris 11.4 CBE | Solaris SSH Test（疎通のみ） |
| 自宅 | 10.0.0.53 / Ubuntu Server | Ubuntu Build and Test |
| 外部 | rocky.ysni.org / Rocky Linux | BBS本番局。OS再起動は許可されるが、更新対象はLiner Notesに限定 |
| 外部 | debian.ysni.org / Debian | BBS本番局。OS再起動は許可されるが、更新対象はLiner Notesに限定 |
| 外部 | srv.ysni.org / FreeBSD | **他サービスと本番共用。OS・SofChat・Apacheの停止／再起動は自動実行厳禁** |

## 既存Actionsの再利用

- `Linux compatibility`：これまでどおりGitHub-hosted runnerでPR等のLinux互換性を検証。
- `FreeBSD Build and Test` / `Rocky Linux Build and Test` / `Ubuntu Build and Test`：従来の`workflow_dispatch`を残し、`workflow_call`でCI統括から再利用。CI管理VMを起点にSSHで自宅検証ホストへソースを送り、`make`と`make check`を実行する。
- `Solaris SSH Test`：従来の手動実行を保持。Solarisは正式移植前なので必須CIゲートには入れない（接続できることとビルド成功は別）。
- `Production SSH Connectivity Test`：外部3局への読み取り専用SSH接続の確認に利用。専用ユーザー`ln-deploy`、鍵認証と固定ホスト鍵照合を維持。

## 新規Actions

### Multi-OS Native CI (`.github/workflows/multi-os-ci.yml`)

`main`へのpushで自動起動する。FreeBSD/Rocky/Ubuntuの3ワークフローを同じコミットで実行し、どれかが失敗・スキップ・キャンセルなら`release-gate`は失敗する。

※ 公開リポジトリのPRコードを自宅Runnerで実行しないため、PRイベントでは起動しない。ブランチ保護を設定し、信頼済みコードのみmainへマージする。

### Production Release Preflight (`.github/workflows/production-release-preflight.yml`)

手動起動のみ。mainの現在のコミットSHA（40桁）を入力し、次の一致を検査する。

1. 選択ブランチは`main`、入力SHAは現在のmainのHEADと一致し、判定の前後にもmainが動いていない。
2. 同じSHAでpush起動した`Multi-OS Native CI`と、FreeBSD 15 pkg / Rocky 10 RPM / Debian 13 DEBの3ワークフローがすべて成功している。
3. 同じSHAに対応するFreeBSD CI完了起動の`Release Set Verification`が成功し、その実行に紐付いた未失効の`liner-notes-release-set-receipt`がある。
4. 同じSHAで手動起動した`Production SSH Connectivity Test`が成功している。
5. 同じコミットでの古い成功履歴を流用して新しい失敗や未完了実行を隠さない。

この判定は`ci/release_admission.py`で行い、GitHub-hosted PRテストで失敗条件を検査します。証跡のSHA-256チェックは暗号署名ではありません。署名済みReleaseや本番ホストのバックアップ状態を示すものでもありません。

現在の前検査は**サーバーへの変更、パッケージ更新、サービス停止・再起動を一切行わない**。GitHub Environment `production-release`を参照する。Environmentを使うだけでは手動承認の実効性はないため、GitHubの管理画面でRequired Reviewers等を設定し、有効性を確認すること。

## 本番自動リリースに必要な後続実装

このPRでは上記前検査までを実装する。本番デプロイを有効化するには次が必要。

1. pkg/RPM/DEBの候補ビルド・同一SHA照合は成功済み。Debian 13は隔離コンテナで同一ソースの0.74-1→0.74-2リビジョン更新を検証済み。ただし**異なるアプリ版の更新・実DB互換・復元を検証したことにはならない**。FreeBSDとRockyの実更新/復元も残件。
2. ビルド元コミット・パッケージのバージョン・SHA-256を一致させて成果物を固定し、署名・検証済み配布物をGitHub Releasesで公開。**本番でビルドしない**。
3. リリース直前の設定差分、依存関係、実行アカウント、DB互換性、共有ボードリンクのプロトコル互換性を検査する。**0.74の共有ボードprotocol 2は0.73との混在運用非対応**。3局のリンクをすべて安全に停止し、整合バックアップを取ってから段階更新し、全局の正常稼働・版一致を確認して初めてリンクを再開する手順と試験が必要。
4. 配布前に各本番BBSの設定・DB・会員・保管庫・リンク鍵・リンクDB・Doorデータを整合した方法でバックアップし、復元可能性も確認する。
5. **Rocky → Debian → FreeBSD**の順に単一のバイナリ成果物を展開し、各局のLiner Notesだけを更新して稼働・BBS間リンクを検証。前局の更新・確認が失敗したら後続は止める。
6. 以前のDBへ戻れないスキーマ更新やリンクプロトコル非互換がある場合、バイナリだけの自動ロールバックを禁止し、手動判断に移行する。
7. リリース承認、再実行防止、最小権限のリモート更新ヘルパー、更新前後の証跡と通知を実装する。

## 本番保護ルール（最優先）

**`srv.ysni.org` は、SofChatの履歴がメモリ上に存在する本番共用サーバーである。** したがって、リリースの承認があってもOSの`reboot`・`shutdown`や、SofChat・Apacheの停止／再起動は禁止。OSや共有パッケージの一括更新もCDでは禁止。自動更新を許可するのは、独立して影響が検証されたLiner Notes専用サービスのみとする。

本番用`ln-deploy`には必要最小限のLiner Notes専用デプロイ権限のみを付与し、無制限のsudo/root権限は付けない。既存本番環境の起動設定・ファイル配置は事前に読み取り専用で調査し、衝突や不明点があれば停止する。意図しないサービス停止が必要な場合は無断実行しない。

## リリース手順（現時点）

1. mainへのpushによるネイティブCI、pkg/RPM/DEB各CI、Release Set Verification、証跡Artifactの成功を確認。
2. 同一SHAの`Production SSH Connectivity Test`を手動実行（読み取り専用）。
3. 同一SHAで`Production Release Preflight (read only)`を手動実行し、すべての検証証跡が同じmainに紐付くか確認。
4. 前検査が成功しても自動で本番更新は起きない。パッケージとOS別デプロイ・復旧手順の整備が完了してから、本番更新ジョブを別PRで追加する。

本番に対して勝手な更新を始めず、段階的に承認された配布体制へ進める。
