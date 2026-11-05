# Liner Notesの概要

Liner Notesは、親記事と返信をツリーでたどるHyperNotes型掲示板です。会員間メール、ファイル保管庫、チャット、個別トーク、Gather伝言板、外部ゲームを備え、TelnetメニューとWebUIから同じBBSを利用します。公開マニュアルの対象は **安定版0.74**、文書改訂日は **2026-11-05** です。

## 機能と入口

| 目的 | 参照先 |
|---|---|
| サーバーを新設する | [FreeBSD](install-freebsd.md)、[Linux](install-linux.md)、[初期設定](initial-setup.md) |
| 利用を始める | [ログイン・登録](accounts.md)、[メニュー](navigation.md) |
| 記事を読む・書く | [ボードと読書](boards.md)、[検索と未読](article-search.md)、[エディタ](editor.md) |
| メール・会話・ファイル | [メール](mail.md)、[チャット・Gather](conversation.md)、[保管庫](files.md) |
| 会員設定・Web | [プロフィール](personal-settings.md)、[WebUI](web-users.md) |
| 管理・運用 | [管理者とSIGOP](permissions.md)、[運用](operations.md)、[バックアップ](backup-recovery.md) |
| 他の接続や外部ゲーム | [Telnet・SSH](connections.md)、[Door](external-games.md)、[シリアル](serial-connector.md)、[共有ボード](links.md) |
| 設定を調べる | [設定一覧](configuration.md)、[上限](limits.md)、[保存方式](storage.md)、[コマンド索引](command-reference.md) |
| 問題を解決する | [トラブルシューティング](troubleshooting.md) |

## システム構成

```text
Telnet端末 ───────────────────────┐
ブラウザー → Apache HTTPS → Webコネクタ ─┤
シリアル端末 → COM/モデムコネクタ ──────┼→ BBS本体 → DB・ファイル本体
他のBBS ←相互TLS→ 共有ボードコネクタ ────┘       └→ PTY → Doorゲーム
```

認証とボード権限はBBS本体で判定します。Web・COM・共有ボードは別プロセスです。管理画面から任意のOSコマンドを実行する機能は設けず、バックアップ・復元・整理は外部管理ツールを使用します。

## 動作環境と利用条件

| 環境 | この版の対応範囲 |
|---|---|
| FreeBSD 15.1 / amd64 | ports・pkg、rc.d、Telnet、Web、PTY Door、管理ツール |
| Rocky Linux 10.2 / x86_64 | RPM、本体・Web・リンクのsystemdサービス。SELinux有効環境での常駐確認あり |
| Rocky Linux 9 / AlmaLinux 9 | Clang・Python 3.11によるソース構築とコンテナ上の回帰検査。RPM実機常駐確認とは別 |
| Ubuntu、Solaris / illumos | この安定版の対応を保証する対象ではありません |
| Windows / DOS | ネイティブサービス、COM、Door互換実行の対応対象ではありません |

本体はC、補助ツール・コネクタはPython 3.11以降を使用します。SQLite・OpenSSLと、選択機能に応じた依存ソフトが必要です。端末はUTF-8とANSI表示を前提にします。Telnetは暗号化されません。Web公開にはHTTPSを使用してください。

Liner NotesはGPL-3.0-onlyです。Web端末の同梱ライブラリにはMITライセンスのものがあります。[ライセンス・同梱物](licenses.md)を参照してください。外部ゲームとlrzszは別配布です。

## この版に含まれないもの

内蔵SSHサーバー、IPv6直接待受、Windows COM、DOS/FOSSIL互換実行、DOOR.SYS、旧RTBBS保存データの直接利用・変換は提供しません。FidoNet対応は計画対象から除外しています。FreeBSDのOpenSSH経由の入口は利用できます。物理モデムと全種類の外部ゲームの動作は、設置者が個別に確認してください。
