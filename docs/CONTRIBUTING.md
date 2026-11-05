# マニュアルの保守規則

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
