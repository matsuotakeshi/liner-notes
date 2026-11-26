# MASH風UIの設計と原典照合

対象は MASH Kernel 0.33b10a の MMM 互換シェルです。オリジナル MMM Revision 4.0 の説明、MASH のモジュール仕様、Midnight Driving の局設定を区別します。

## 原典確認

| 資料 | 確認できた内容 |
| --- | --- |
| [Midnight Driving](https://webmid.kinet.ne.jp/)・[操作ガイド](https://webmid.kinet.ne.jp/mid/manual/wtsbbs/) | 稼働情報には Kernel 0.33b10a と記載。番号付きの主メニューは同局の実例。Telnet の案内は Shift-JIS。 |
| [USER](https://webmid.kinet.ne.jp/mid/manual/wtsbbs/manual/MASHMAN/USER-utf8.txt) | MMM Revision 4.0 の利用者マニュアル。メニュー、プロンプト、コマンドの短縮とヘルプの比較資料。 |
| [COMMANDS](https://webmid.kinet.ne.jp/mid/manual/wtsbbs/manual/MASHMAN/COMMANDS-utf8.txt) | MASH のコマンドリファレンス。接続一覧・電報は §4.7、ニュース・個人ファイルは §4.8。モジュールの設定によって利用できる操作が異なる。 |
| [APPENDIX](https://webmid.kinet.ne.jp/mid/manual/wtsbbs/manual/MASHMAN/APPENDIX-utf8.txt) | MASH の操作例。コマンドの連携例は局の構成に依存する。 |
| [HNOTES](https://webmid.kinet.ne.jp/mid/manual/wtsbbs/manual/MASHMAN/HNOTES-utf8.txt) | MASH 配布資料の HyperNotes 説明。ベース記事、レス、既読巡回、返信の基本操作を確認。PLINK の専用書式は公開資料で確定できなかった。 |

公開されている資料を参照し、原典の本文・原コード・メッセージファイルは取り込まない。稼働局への実接続試験は行っていない。

## 実装前の対応表

| 機能 | 現行 MMM の実装 | MASH の設計 |
| --- | --- | --- |
| 主メニューと設定 | 数字 1–7、`term`、`profile`、`games` | 局の実例の番号に合わせ、MASH 独自の補助メニューを追加する。ゲームは Liner Notes 拡張として提供。 |
| 操作案内・接続一覧 | 画面別 `?`、Chat Core の在席一覧 | `manual`、`cmdlst`、`-?`、`who` と短縮キーを追加する。 |
| 掲示板・マップ | ACL 付き階層一覧、HyperNotes 読書・編集 | `bbs` と `map` は同じ階層一覧を使う。 |
| 親子関係 | Article Core の親 ID と可視性判定 | 既存の親 ID を表示し、通常閲覧の ACL を通して直接の親へ移動する。専用書式は Liner Notes 拡張。 |
| 新着・一行会議 | `new -n`、`talk -n`、共通の既読状態 | 共通 Core を利用する。話題ニュースは独立した公開ボードの読書へ割り当てる。 |
| 電報・SYSOP 呼び出し | Chat Core の個別送信・管理者呼び出し | `!` と `$` から同じ Core を使う。強制・一斉・ファイル入力・OS 実行を扱わない。 |
| メール・保管庫・個人ファイル・Door | 既存の MMM アダプターと共通サービス | 記事、メール、個人設定を共有し、UI の一時バッファはアダプターごとに分離する。 |
| 端末・外部連携 | Telnet・SSH・RAW・UTF-8 | 現行 Transport を使う。MASH Gateway や Shift-JIS 変換の再実装は対象外。 |
