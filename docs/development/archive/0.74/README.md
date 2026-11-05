> Liner notes 0.74です。今回の実装・検証範囲は [STAGE74.md](STAGE74.md)、DB変換は [DATABASE_STRUCTURED.md](DATABASE_STRUCTURED.md)、COMは [SERIAL.md](SERIAL.md)、共有ボードは [BBS_LINKS.md](BBS_LINKS.md) を参照してください。

> 現行版は0.74です。[変更内容](STAGE74.md)、[更新手順](UPGRADING.md)を参照してください。既存DB形式は自動変換しません。

# Liner notes 0.74

今回の変更は [STAGE74.md](STAGE74.md)、更新は [UPGRADING.md](UPGRADING.md) を参照してください。互換モードは [STAGE69.md](STAGE69.md) に記載しています。

FreeBSD向けのC製BBSです。Rocky Linux／AlmaLinux向けの構築・インストール手順も備えています。HyperNotes型の返信ツリー、会員間メール、チャット、ファイル保管庫を備え、メニューとHELPは日本語・英語に対応します。

0.74ではLinux RPM、リンク管理・履歴回収・新規参加用スナップショットを追加しました。Rocky Linux実機の再起動と大容量同時転送を検証しています。[変更・検証範囲](STAGE74.md)、[RPM](RPM.md)、[リンク保守](BBS_LINK_OPERATIONS.md)を参照してください。

## 最初に読む文書

- **[詳細導入マニュアル（0.74・2026-11-04改訂）](INSTALL.md)** — ダウンロード照合、管理者登録、ポート23、Apache/WebUI、ゲーム、バックアップ、トラブル対応。
- [WebUI・別プロセス構成と導入](WEBUI.md)
- [Rocky Linux / AlmaLinuxでの構築・systemd](LINUX.md) / [対応OSの見通し](PLATFORMS.md)
- [今回の変更](STAGE74.md) / [既存環境の更新](UPGRADING.md)
- [旧RTBBS互換モード](STAGE69.md)
- [SQLite移行・バックアップの仕様](STAGE67.md)
- [RTBBSとのコマンド対応・残る差分](COMMANDS.md)
- [FreeBSD portsによる導入とrc.conf](PORTS.md)
- [FreeBSD外部ゲームの登録](DOORS.md)
- [バックアップ・復元・別コピーへの整理](MAINTENANCE.md)
- [起動方法・外部HELP・プロフィール等](STAGE61.md)
- [現行上限・階層権限・登録方式・新着巡回](STAGE64.md)
- [過去の開発記録](HISTORY.md)（当時の制限や未実装表は現状と異なります）

## ビルドと試運転

以下はFreeBSDでの手順です。Linuxでは[LINUX.md](LINUX.md)の依存パッケージ・Clang・Python 3.11の指定を使用してください。

FreeBSDのCコンパイラ、libcrypto、SQLite3（FTS5/trigram）、検証・転送ツール用のPython3が必要です。

```sh
make
./runtime --telnet --menu --lang ja 2222 notes.dat members.dat
telnet localhost 2222
```

この指定では127.0.0.1待受です。外部公開時は `--bind 0.0.0.0` 等を明示します。Telnetは暗号化されません。SSH経由の入口は `scripts/liner-notes-ssh`、常駐起動は付属rc.dを使います。メニューを使う場合は `--menu`、Telnet接続には `--telnet` が必要です。

SQLite原本保存、タイムゾーン、運用上限は[stage67](STAGE67.md)を参照してください。既存環境の切替は移行先コピーの検証後に行います。

インストール後は `/usr/local/etc/liner-notes/liner_notes.conf` と `/etc/rc.conf` で設定します。rc.confの指定が優先されます。portの設定例に従ってパスを指定してください。ゲームを使う場合は `liner_notes_doors="/usr/local/etc/liner-notes/doors.conf"` を追加します。

記事・メール本文は最大256KiB、入力行4096バイト。会員IDは999999、ボード1024、単一ファイル1GiB、一括16ファイル/16GiB、同時接続128、同時転送24が安全上限です。limits.confでそれ以内の運用値を指定できます。記事・メール件数と投稿者別件数の運用制限は0で無制限にできますが、IDの表現上限は残ります。

メール本文と添付は送信者・受信者のみが閲覧できます。SYSOPにも閲覧の例外は設けていません。旧RTBBSの保存ファイルを直接読み込むものではありません。

## 検証

`make check` が通常の回帰試験です。`make test-stage69 test-stage70 test-stage71` で操作・権限の試験、`make test-stage72 test-stage73 test-stage74 PYTHON=/path/to/web-venv/bin/python` でDoor/WebUI・新コネクタ、実ZMODEMは `make test-zmodem LRZ=/path/lrz LSZ=/path/lsz` です。0.74の結果はports配布セットまたはリポジトリの `evidence/stage74/` を参照してください。

## ライセンス

Liner notes本体は **GPL-3.0-only** です。[LICENSE](LICENSE)と[第三者ソフトの表記](THIRD_PARTY.md)を参照してください。

共有ボードリンクとCOMは明示的に設定・起動するまで無効です。設定・データの自動変換やサービスの自動再起動は行いません。
