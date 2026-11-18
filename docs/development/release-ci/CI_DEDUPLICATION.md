# CI重複実行の整理

2026-11-17。基準main: `87319a3b327126de95b7a504d757a74bb97dbf41`。
本変更は起動条件だけを調整し、試験内容・必須チェック・ブランチ保護・自宅Runnerの信頼境界は変更しません。

## 変更と削減数

| ワークフロー | 起動条件 | 削減される実行 |
| --- | --- | --- |
| CI scope checks | PR・手動を維持、main pushを削除 | 各マージ後1回 |
| Shared board visibility | PR・手動を維持、main pushを削除 | 各マージ後1回（実試験は従来どおり変更範囲判定） |
| Native CI Source Checks | PRの既存パス条件・手動を維持、main pushを削除 | 対象パス変更のマージ後1回 |
| Canonical manual | PR、main/linux-support/release/** push、全タグ、手動 | 通常の開発ブランチpushごと1回。PR検証は残す |

削減数はワークフロー単位です。ジョブ数や所要時間の削減率は測定していません。
通常のマージは2本、Native CI Source Checks対象の変更なら3本減ります。
開発ブランチにPRがない間はマニュアルが自動生成されないため、PRを開くかmainでの手動検証を利用します。

## 変更しない依存関係

`ci/deploy_release.py` の `GATES` は同一main SHAの実行とArtifactを照合します。
この依存関係をPRの過去結果へ置き換えません。

- Linux compatibility、doctor、Deployment safety、Canonical manualのmain検査。
- Multi-OS Native CI、Solaris、SELinuxのmain限定実機検査と共通排他。
- FreeBSD/Rocky/Debianパッケージ生成・導入試験。
- FreeBSD package完了を入口にするRelease Set VerificationとFreeBSD upgrade acceptance。
- production preflight/配備の承認・手動起動条件。
- 既にPR/手動のみのpackage lint、release-set lint、その他手動ワークフロー。

依存のあるhosted検査のPR/main重複は残ります。削減する場合は、配備証跡の取得条件を別途設計・検証する必要があります。

## 品質ゲートと制約

2026-11-17のGitHub API確認では、mainの有効なrulesetにPR必須、strictな
`linux (almalinux:9)` / `linux (rockylinux/rockylinux:9)`、bypass actorなし、
force push/削除禁止が設定されています。classic branch protectionの404は
ruleset保護がないことを意味しません。本変更でこれらの設定は操作していません。

PR専用へ移す3本は現在の必須チェック一覧には含まれません。適用されるPR検査の
成功をレビュー時に確認する運用が必要です。統合時だけの問題はPR結果では保証
できないため、mainのネイティブ回帰と既存のパッケージ検査を残します。
リリース前には必要に応じて3本をmainのworkflow_dispatchで実行し、実際のSHAを
記録します。特定のSHAの検証が必要な場合、起動後のhead SHAが候補と一致することを
確認してください。古いPR結果を新しいmainの証跡として利用しません。

本変更ではCIを手動で大量起動せず、ローカルの起動条件・依存契約検査と通常のPR CIを
利用します。マージは利用者確認後に行います。

## マニュアル根拠の再照合

PR #65の初回CIでは、Canonical manualとRPM/DEBのマニュアル生成検査が
`CI_CD.md` / `.github/workflows/native-ci-lint.yml` のsource-lock不一致で停止しました。
2026-11-17に基準mainとの差分を確認し、前者は重複起動方針の追加、後者は
hostedソース検査のmain push削除だけであり、PRの検査内容とmain限定ネイティブ
試験は変わらないことを再確認しました。

公開原稿のinstall-solaris.mdの「PRでは接続を伴わないCI制御のテスト」、
install-linux.mdの同一SHAによるRelease Set・パッケージ検証説明との矛盾は
ありません。利用者向けの導入・更新・保存形式の仕様変更もありません。
開発運用の詳細は本書とCI_CD.mdに集約し、公開原稿や検査条件は変更せず、
確認済みの2ファイルだけsource-lockを更新します。旧アーカイブは変更しません。

修正後のローカル検証: `scripts/manual/build.py build/pr65-source-lock/manual --zip`、
`scripts/manual/check.py build/pr65-source-lock/manual --report build/pr65-source-lock/validation.json`、
`node scripts/manual/test-search.cjs build/pr65-source-lock/manual` が成功。
正本42ページ、HTML 131ページ、内部リンク2,812件、旧URL 172件、検索8ケースと
Markdown/HTML/ZIPの一致を確認しました。ブラウザー描画・RPM/DEBの再生成は
このローカル検証には含めず、通常のPR CIへ委ねます。

## Solarisの自動実行頻度

Solaris Native CIは毎日1回（UTC 19:23、日本時間04:23予定）のscheduleと、main限定workflow_dispatchに変更しました。マージごとのpush起動は行いません。定期実行は既存scope判定で全検証となり、main/repository/SHA検査と同じ排他を維持します。配備判定は同一SHAのschedule/manualと従来pushの最新結果を検証し、別コミットの成功で代用しません。手動・再実行を含む厳密な24時間レート制限ではありません。
