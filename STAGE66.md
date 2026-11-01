# stage66 — RTBBS風コマンド整理

旧RTBBS 5.3βbのMAINSYS / MAILSYS / FILESYS / EDITSYS / PROFSYS / SYSOPCMDを照合し、メニュー画面のキーを機能別に整理しました。対応表と、揃えていない操作の一覧は[COMMANDS.md](COMMANDS.md)にまとめています。

- 主画面、掲示板、読書、設定、プロフィールを区別し、現在の画面名を入力案内に表示。
- 主画面はB掲示板・F保管庫・Mメール・E共通エディタ・I個人設定・O終了等。
- 読書中はEnter次・P前・L再読・D親・A返信・G本文取込・K取消確認・E担当者ヘッダ編集。
- 掲示板・メール・保管庫のNは日付指定。空欄はCHECK、確認日時の保存はCHECK DONE。
- ベタ入力の//はW書込・E再編集・0取消へ。拡張操作の.とrawモードは従来どおり。
- 保管庫のU/D/番号入力は選択中の転送方式を使用。FILE番号は詳細表示。Bはバッチ案内。
- 操作履歴の最新表示はAUDIT REFRESH。現在の絞り込みを保持。
- 日本語/英語の外部メニュー・HELPを更新。8個のサブメニュー外部ファイルを追加。
- 保存形式・記事権限・管理権限・メールの非公開範囲は変更なし。

## 更新時

旧一文字キーの意味が変わります。COMMANDS.mdの移行表を確認し、操作マクロは名前付きコマンドへ変更してください。実行ファイルだけでなく外部メニューとHELPも同時に更新します。portsの編集済み@sampleは自動上書きされません。稼働中サービスの切替は自動実行しません。

## 検証

`make check`と`make test-stage66`、実転送は`make test-zmodem LRZ=/path/lrz LSZ=/path/lsz`。実際の実施範囲・結果は配布物の`evidence/stage66/VERIFICATION.md`を参照してください。FreeBSD portの配置は[PORTS.md](PORTS.md)を維持します。
