# NET-COCK Ver.3.60 Quintetの参照資料

調査日は2026年11月27日。稼働局への接続は行わず、公開された操作説明を調査した。Liner Notesには各資料から確認した操作を独立実装する。原作と互換実装のソース、表示文、素材は取り込まない。

| 確認区分 | 資料と項目 | Liner Notesの対応先 |
|---|---|---|
| 原作確認済み | [T.C.M.networkの操作ガイド](https://tcm.jp/member/guide.html)。ゲスト向け5分類、ログイン時の前回／今回日時、会員レベル、持ち時間の表示。番号と局独自の値は運営例。 | Session / Member / Activity |
| 原作確認済み | [ボードの読み書き](https://tcm.jp/kusanone/tcm02.html)。BREAD、BWRITE、グループA〜O、BSET、RALL、RNALL、PMOVEとバッチ完了時の切断。RNALL Yは明示的なメール削除を伴う。 | Article / Traversal / Vault / Batch |
| 原作確認済み | [リード・サブコマンド](https://tcm.jp/kusanone/tcm05.html)。ヘッダー先行、分単位の時刻、Rで本文、N/B/T/Eで移動、Pは選択中、Fは最後に読んだ記事、U/Iは10階層。X/Y/Sはプログラム用。 | Article Coreの平面一覧、可視性と既読判定。 |
| 原作確認済み | [書き込み](https://tcm.jp/kusanone/tcm06.html)。題名・本文、単独の点、Y/Enterで確定、Cで確認、T/Eで編集、Nで破棄、Qで保持。 | Editor / Draft / Mail |
| 原作確認済み | [メール](https://tcm.jp/kusanone/tcm03.html)。MREAD、MWRITE、MCHECK、MBSET。4宛先と管理者例外は歴史的な仕様。 | 配達・閲覧認可をMail Coreに委譲する。 |
| 原作確認済み | [チャット](https://tcm.jp/kusanone/tcm04.html)と[電報](https://tcm.jp/kusanone/tcm08.html)。CHAT、LLIST、電報・受信設定、回線表示。 | Chat Coreの接続ID・世代・受信設定。 |
| 原作確認済み | [コマンドライン](https://tcm.jp/kusanone/tcm09.html)。B/BW、BA/BS、RA/RN、O等。Mは設定とメールの両方に掲載される。 | 完全名を優先し、重複する短縮の順序を固定する。 |
| 互換実装で確認 | [null-net-cock](https://github.com/GOROman/null-net-cock/tree/ba5d14ed5a29c78927ea58a416f08a2cdba89d29) の `src/session.c`、`cmd_board.c`、`cmd_chat.c`、`cmd_member.c`、`cmd_sys.c`、`docs/usage.md`、`tests`。メニュー9分類、完全名と優先順、読書、投稿、バッチ、ドットコマンドを確認。MIT表示も確認。 | 原作文書に曖昧な箇所の参考。MODEのMを先に解決する。 |
| 互換実装で確認 | [KTC-BBS](https://www.wdic.org/w/TECH/KTC-BBS)と[MPNBBS](https://www.wdic.org/w/TECH/MPNBBS)の資料。KTC-BBSの秒表示、MPNBBSの選択式ヘッダーは派生実装固有の参考。 | 原作仕様の根拠には含めない。 |
| 独自拡張 | 共通一行会議、TOPIC、ZMODEM、下書き、Door、UI選択、既存ACL。 | Liner Notes共通Core |
| 未確認 | 原作マニュアル全巻、回線ごとの表示設定、旧会員番号体系、原作の全権限レベルと全テンプレート。 | 互換を表明しない。 |

原典の局で公開された時刻は分まで。ローカルの表示上のID桁数では内部IDを制限しない。旧式のスラッシュ連結による自動巡回は転送・削除の順序保証が必要なので別検証とする。
