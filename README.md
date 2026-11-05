# Liner Notes

HyperNotes型のツリー掲示板、会員間メール、チャット、ファイル保管庫、WebUI、PTY Doorと公開鍵による共有ボードを備えたBBSです。現行安定版は **0.74.1** です。

## 統合マニュアル

- [公開マニュアル](https://linernotes.site/manual/)
- [Markdown原本と統一目次](docs/manual/manifest.json)
- [FreeBSD導入](docs/manual/install-freebsd.md) / [Linux導入](docs/manual/install-linux.md)
- [利用者ガイド](docs/manual/accounts.md) / [管理者・SIGOP](docs/manual/permissions.md)
- [運用と復元](docs/manual/backup-recovery.md) / [外部連携](docs/manual/connections.md)
- [設定](docs/manual/configuration.md) / [トラブルシューティング](docs/manual/troubleshooting.md)

現行機能の説明はdocs/manualに集約しています。ルートに残る旧名の文書は移転案内です。開発経過・試験記録を読むことは通常の利用条件ではありません。

## 開発

Cのビルドは `make`、回帰試験は `make check`。OS別の依存と配置は統合マニュアルを参照してください。マニュアルの生成・検証・今後の更新規則は [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md) にあります。

[変更履歴](CHANGELOG.md)、[開発アーカイブ・統合対応表](docs/development/manual-consolidation/MAPPING.md)、既存の `evidence/` は開発資料です。

ライセンスは [GPL-3.0-only](LICENSE)。第三者の同梱物は [THIRD_PARTY.md](THIRD_PARTY.md) を確認してください。外部ゲームとlrzszは別配布です。
