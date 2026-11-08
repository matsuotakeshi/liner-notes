# 開発・文書の保守規則

## 全バージョン共通の開発フロー

[CI/CD恒久運用方針](../CI_CD.md)を、機能追加・修正・移植・パッケージ更新の標準とします。既存GitHub Actionsと検証設備を継続利用し、手動検証だけでは開発完了としません。

1. 最新mainからブランチを作り、実装・自動テスト・該当マニュアルを更新する。
2. PRを作成し、適用されるGitHub-hosted必須CIとレビューを通す。
3. mainへ統合後、同一SHAのFreeBSD・Rocky Linux・UbuntuネイティブCIを確認する。自宅Runnerは信頼済みmain専用とし、未審査PRを渡さない。
4. 必要なpkg/RPM/DEB生成・導入/更新/削除・データ/設定保持の検証を行う。リリース候補は原則としてCI生成物を使い、ソースSHA・版・SHA-256を照合する。
5. CI失敗の原因を解消または再検証し、Actionsログ/Artifactを報告に結び付ける。失敗の無視、テスト無効化、条件緩和で完了扱いにしない。

新機能ではOS互換性、後方互換性、日本語/英語UI・HELP、WebUI・リンク・Door、バックアップ/復元の影響を検討し、必要なテストをCIへ追加します。既存設備で検証できない範囲は制約として記録します。

本番3局を開発試験に使用しません。パッケージ公開と本番配備は分離し、配備には別途の明示承認と必要なCI・前検査・バックアップ/復元確認が必要です。接続先・鍵・保護対象の詳細は上記方針を参照してください。

## マニュアルの保守規則

公開マニュアルの正本は `docs/manual/` のMarkdownとmanifestです。対象はmanifestに記載した現行安定版です。開発途中のコードと公開済み仕様を混ぜません。

1. 機能追加・変更では該当する機能章、コマンド、設定・既定値、上限、権限、運用・復旧手順を同時に更新する。
2. 新しい機能ページが必要な場合だけmanifestへ追加し、7章の統一目次から案内する。
3. CHANGELOGには変更の要点を記録する。開発過程・試験結果は `docs/development/` または `evidence/` へ置く。
4. `STAGE*.md` や検証成果を公開原稿として追加しない。ルートMarkdownを一括公開する生成処理を再導入しない。
5. 廃止・統合時は有効情報を移し、legacy-urls.jsonと対応表を更新する。リンクと旧アンカーを確認してから公開する。
6. 説明は現行ソース・設定・テスト・正式リリースで確認し、不明な点は開発側の確認事項へ記録する。未検証環境を対応済みと書かない。
7. 生成HTMLを手で修正しない。Markdown原本から公開版とオフライン版を同時に生成する。

## 生成と検証

Python 3.11以降で、専用環境へ `scripts/manual/requirements.txt` を導入します。

```sh
python3 -m venv .manual-venv
.manual-venv/bin/pip install -r scripts/manual/requirements.txt
.manual-venv/bin/python scripts/manual/build.py /新しい出力先 --zip
.manual-venv/bin/python scripts/manual/check.py /新しい出力先 --report /検証結果.json
```

Windowsでは `.manual-venv/Scripts/python` を使用します。既存出力先への上書きは拒否します。標準のMarkdown rendererだけを使用し、ネットワークから原稿を取得しません。

`docs/development/manual-consolidation/source-lock.json` は説明確認に使用したソースのSHA-256です。変更を検出したら、該当説明のレビューと証拠更新を行います。CIを通すためだけにハッシュを置き換えないでください。

サイト側の `scripts/import-manual.py` はこの生成結果だけを取り込み、旧文書を追加生成しません。サイト更新ZIPは手動アップロード用です。生成・リンク・検索・表示・ZIP検査を通してから設置者が反映し、実URLで確認します。

アプリの正式リリースに含まれる旧原稿を変更したことにはなりません。次のソース・パッケージ配布でもこの正本を文書として使い、開発記録の同梱先を利用マニュアルと分離してください。
