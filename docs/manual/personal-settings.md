# プロフィールと個人設定

## プロフィール

`PROFILE` は自分、`PROFILE ログイン名` は相手、`PROFILES [検索語]` は一覧・検索です。`+` で続き、番号で詳細を表示します。利用停止会員は通常一覧に表示しません。

| 項目 | 内容 |
|---|---|
| HANDLE | ハンドル。80バイトまで。重複可、ログイン名は変えない |
| BIO | 1行自己紹介320バイト。BIOEDITの複数行自己紹介は8192バイト |
| LOCATION / OCCUPATION / INTERESTS | 地域・職業・趣味 |
| WEBSITE / CONTACT | URL・公開連絡先 |
| MACHINE / BIRTHDAY | 使用機種・誕生日（YYYY-MM-DD） |
| REALNAME / KANA / ADDRESS / PHONE | 氏名・カナ・住所・電話 |

`PROFILESET 項目 本文` で編集、`PROFILEVIS 項目 PRIVATE|MEMBERS|MONTHDAY` で公開範囲を選びます。MONTHDAYは誕生日の年を隠す指定です。HANDLEは会員公開、新しい個人情報項目は初期非公開です。本人・管理者の扱いと他会員への公開を混同しないでください。保存後の旧記録が原本やバックアップに残る場合があります。

`PROFILEFIND 項目 検索語`、`PROFILEFIND GROUP 1..100` で項目検索します。非公開情報を検索結果から漏らさないよう公開範囲を適用します。プロフィール画面のEは項目番号で編集、Sは項目と条件で検索。Webは全13項目を表示します。

任意の互換ポリシーが有効な場合、住所・誕生日の本人変更は管理者へ依頼し、グループ検索はSIGOP以上、氏名・カナ・住所・電話の項目検索は管理者に限定されます。既存の公開設定を一括変更する機能ではありません。

## 会員設定

| 命令 | 設定 |
|---|---|
| LANG JA / EN | 日本語・英語 |
| PAGER ON / OFF、PAGER SIZE 列 行 | ページ停止・端末サイズ |
| EDITOR BETA / LINE | エディタ |
| MENUMODE BEGINNER / STANDARD / EXPERT | メニュー案内量 |
| PROTOCOL 方式 | 端末転送方式 |
| MAIL NOTIFY ON / OFF | メール新着通知 |
| /TALK ON / /TALK OFF | 個別トーク受信 |
| CHECKMODE AUTO / MANUAL | 巡回確認日時の更新方式 |

`PREFS` または設定画面Dで現在値、`PREFS SAVE` で保存、`PREFS LOAD` で再読込します。ログイン時にも復元します。通常のログアウト・終了と通信断では表示設定の保存を試みます。エディタの行番号とPROTOCOLも対象ですが、編集中の検索語・行カーソルは対象外です。

`USERLOG` は接続履歴、`USERTABLE` は接続単位の表示、`USERSTATS` は利用時間・ログイン回数です。通常は本人分、管理者はALLを指定できます。会員へのUSERLOG公開を設置者が有効にしている場合は公開範囲が広がりますが、ゲストには開放しません。
