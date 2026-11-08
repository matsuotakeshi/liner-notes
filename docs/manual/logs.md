# 監査ログ・接続履歴・取得回数

| 履歴 | 内容・入口 |
|---|---|
| AUDIT | 記事の取り消し・題名・投稿者変更、管理操作 |
| BOARDLOG | ボード設定変更 |
| BOARDACCESS 番号 | ボードの閲覧・投稿履歴 |
| FILEAUDIT | 保管庫の登録後変更・方式記録 |
| FILEHISTORY / FILESTATS | 取得履歴 / 完了確認済み回数 |
| USERLOG / USERTABLE | 接続履歴 / 接続単位の表 |
| USERSTATS | 累計接続回数・利用時間 |
| チャットログ | 設定済みの保存先へ共通発言を保存 |

`AUDIT ARTICLE 番号`、`AUDIT ACTOR ログイン名`、`AUDIT ACTORID 会員番号` は記事・操作者の絞り込みで、併用できます。`AUDIT REFRESH` は再取得、`AUDIT ALL` は全体、`+` は続きです。操作権限・担当範囲を確認します。

USERLOGは通常本人分、管理者は `USERLOG ALL` / `USERSTATS ALL` を使います。任意の会員公開設定は[会員設定](personal-settings.md)を参照してください。過去のUSERLOGにある接続番号を現在の強制切断へ使わず、CONNECTIONSで現在の接続を確認します。

取得完了は転送コネクタの個別完了通知に基づきます。一括途中失敗でも成功分の履歴は保持し、未完了を成功にしません。WebではHTTPへの送出完了であり、利用者のディスク保存完了までは保証しません。

ログの保存先は[設定一覧](configuration.md)。保持件数は全体またはボード設定、日数整理は[外部管理ツール](backup-recovery.md)で明示します。集計・業務IDは保持しても整理後の内部連番・接続番号は変わる場合があります。監査証跡を必要な期間保存してから整理してください。
