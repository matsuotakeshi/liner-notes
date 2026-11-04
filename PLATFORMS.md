# 対応OSと移植の見通し

| 環境 | 状況・追加作業 |
|---|---|
| FreeBSD 15.1/amd64 | 現行リリースの検証環境。ports、rc.d、実ZMODEMを検証済み。 |
| Rocky Linux 9 / AlmaLinux 9 x86_64 | Clang/Python 3.11ビルド、インストーラー、systemd設定を提供。[0.73コンテナCI](https://github.com/matsuotakeshi/liner-notes/actions/runs/41783614429)で通常・拡張回帰、実ZMODEM、Web、PTY、COM、共有ボード、導入後起動を検査。合否・ログは `evidence/stage73/` を参照。systemd自動起動とSELinux実機運用は未確認。 |
| その他のLinux | 同じPOSIX実装を使える見込み。各ディストリビューションの依存ライブラリ、常駐起動、実転送の確認が必要。 |
| 他のBSD / macOS | 移植候補。PTYヘッダー、ライブラリ、ファイルロック・同期、常駐起動の違いを確認する。未検証。 |
| Solaris / illumos / AIX等 | POSIXという共通点はあるが、一括して対応済みとは扱わない。OS・バージョン・CPU・ツールチェーンを決めた実機調査が必要。 |
| Windows上のWSL2 | Linux版を利用する候補。Windows側の公開ポート、自動起動、停止・復帰は別途検証が必要。現在のLinux CIはWSL2を検証していない。 |
| Windowsネイティブ | 未対応。旧来のWindows用分岐だけでは現行機能は揃わない。下記の別移植が必要。 |

現在の`runtime.c`にはWinsockの分岐があります。一方、`storage_sqlite.c`はpthread、fcntlのファイルロック、POSIXのパス・ファイル操作に依存します。Pythonの管理・転送ツールにもfcntl等への依存があります。`zmodem_host.c`のWindows分岐は転送未対応を返す実装です。ドアゲームもPOSIX PTYと子プロセス制御に依存します。

Windowsネイティブ化では、保存領域の排他・同期・置換、外部転送プロセスと通信路、ConPTY等によるゲーム端末、Windowsサービス、パス・権限モデルをOS依存層へ分離する必要があります。記事・メール・会員・ボード権限の共通ロジックは維持する方針が適切です。これはソースを基にした見通しで、Windows版の動作確認結果ではありません。

今回のLinux対応に、他のUNIXやWindowsネイティブ版の実装・公開は含めていません。

参考: [MicrosoftのWSL概要](https://learn.microsoft.com/en-us/windows/wsl/about)、[WSLのsystemd](https://learn.microsoft.com/en-us/windows/wsl/systemd/)、[Windowsの疑似コンソール](https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session)。WSLにsystemdがあることと、本アプリのWindows常駐運用を検証済みであることは別です。
