# KTBBS 6.21A 操作仕様の照合

Liner Notesの基準は `e939a9d280107ea939e861bbd39e468fb742100d`（監査タグ `audit-2026-11-27`）。[Issue #54](https://github.com/matsuotakeshi/liner-notes/issues/54) のCore/UI、選択ポリシー、既定値を適用する。#193 と #197 は統合済みで、`v0.75` は既存の公開版を指す。現行は RTBBS・MMM・MASH の3端末UIが登録されているため、追加後は4種になる。

## 原典

Vectorの配布ボタンからたどり、取得サイズ、アーカイブのCRCとエントリー名を検査した。日本語の本文、定義、PascalソースはCP932としてデコードした。展開・閲覧はリポジトリ外の隔離領域で行った。アーカイブ内のEXE、BAT、OBJは実行・リンクしない。引用したコマンド名以外の原文、Pascal、設定サンプルは配布しない。

| 資料 | 内容と取得したファイルのSHA-256 |
|---|---|
| [ユーザーズマニュアル](https://www.vector.co.jp/soft/dl/dos/net/se009531.html) | `KTBSUSER.MAN`、43,445 bytes、`93eb7c157161dca5270384798ee171ec75d0f5f950ec042c58122e66ad8fd21a` |
| [SYSOPマニュアル](https://www.vector.co.jp/soft/dl/dos/net/se009515.html) | `KTBSYSOP.MAN`、97,625 bytes、`8226e2f3d8d585a22d9aa7aeef6929c477ed94313129b157b90c494134074027` |
| [ソース](https://www.vector.co.jp/soft/dl/dos/net/se023278.html) | `README.SRC`、`BBS3.PAS`、`BBS98E.PAS`、`MAILSYS.PAS`、`FILESYSA.PAS`、`SYSOPCMD.PAS` など、227,680 bytes、`7b8c07bcc1b22bcfa32710c9c25da6173251c9ffc5630d4cdb4b8ba0b5cc76b9` |
| [システムメッセージ・定義](https://www.vector.co.jp/soft/dl/dos/net/se023279.html) | `DEFINE.DEF`、`BOARDS.DEF`、`FBOARDS.DEF`、キーテーブル、メッセージ内包アーカイブ、20,330 bytes、`9a7e73633140af3df5604a89b7e2d7912510db7bd5320181b7f2d5c340262a98` |

`KTBSYSOP.MAN` の配付条件と著作権節は原作者とKPUCの権利を明記し、改変配布にも条件を適用する。GPLで配布する実装には資料のコード・文言・アートを組み込まず、動作上の事実に基づく新規コードと独自の表示を用いる。

## 実装前の対応表

行番号は `KTBSUSER.MAN` をCP932として読み、CRLFを1改行と数えた位置。標準配布と追加ユニットは分ける。

| 領域 | 原典で確認した操作 | 確認度と共通処理への接続 |
|---|---|---|
| ログイン | 249–320行。ID、続けて秘密入力。標準設定でゲストID・許可内容を局が設定できる | 確認済み。Member/Session Core。既存ID・局の認証方式に従う。原典の認証回数や自動ゲストは移さない |
| 主メニュー | 310–322行。R 読書、E 書込、M メール、N 新着、F 保管庫、C チャット、I 端末、G 終了。A/H/O/U/W/X/Y/# 等 | 確認済み。専用のキー・サブメニュー。局固有文言は独自表示 |
| 掲示板 | 487–674行。ボード番号・ラベル、? 一覧、R/I/S/L/#、題名と本文を切り替える読書、N/P/R/+/-/0 | 確認済み。Board/Article Core。画面に振る番号と永続IDを別に管理し、各読書でCoreが再照合 |
| 新着巡回 | 445–485行。前回ログイン基準、日付・時刻、開始ボードの指定、@ 連続、C で中断 | 一部確認。Traversal/Checkpoint Coreにある保存・日時・連続再生を利用。前回ログインとの違いと旧書式の対応範囲を利用者向けに示す |
| 投稿エディタ | 695–775行。E→W/R/M、行単位、`//`で編集メニュー、S 送信、L 再読、C 追加、0 中止 | 確認済み。既存Editor Coreと送信要求。既存の容量・添付・下書き |
| メール | 870–1058行。E→M 送信、M で読書、RETで順次、Lで受信・送信一覧、本文読書 | 一部確認。Mail Core。混在時の配列順、同報グループ、Bメールの旧手順は別項目 |
| 保管庫 | 1165–1529行。F でボード選択、L/S 等、転送方式ごとのU/D | 確認済み。Vault Coreと既存転送。カテゴリパス、添付、再開、複数選択はLiner Notes側の機能 |
| チャット | 2070–2199行。C、W、T、S、`//`。電報は接続チャンネル宛 | 確認済み。Chat/個別トーク Core。旧電報の配送確認・待ち行列回数、室全体のログ切替は再現対象を別に判断 |
| 会員設定・管理 | ユーザーズのInstall/Profile、SYSOPマニュアルの設定と運用章、`DEFINE.DEF` | 一部確認。Preference・Member・Admin Core。旧DOSのホストメニューと保守コマンドは対象外 |
| 外部ゲーム・一行会議 | 標準配布でLiner NotesのDoorと同じ機能、または一行会議の互換性を確認できていない | 未確認。既存Doorと一行会議をLiner Notes拡張として提供する |

KTBBSの画面状態と入力文法は独立したアダプターに置く。既存の端末UIで型付きCore要求を作る処理、結果、エディタ、転送、通知が再利用できる箇所は共有する。Webコネクタは既存のRTBBSアダプター経由を維持する。
