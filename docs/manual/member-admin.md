# 会員・申請の管理

## 登録方式とID

`registration` はopen / approval / closed、`registration_form` はsimple / detailed、`id_mode` はfree / autoです。自動IDの接頭辞・桁数・初回IDも設定できます。最初のID指定は空の会員台帳の最初だけです。既存名と永久欠番を保持し、採番した名前へ管理者権限を自動付与しません。

## 申請を確認する

`APPLICATIONS` → `APPLICATION 会員番号` で内容を確認し、必要なら `APPLICATIONSET 会員番号 項目 値` で修正して `APPROVE 会員番号` で承認します。却下は `REJECT 番号 CONFIRM`、申請削除は `DELETEAPPLICATION 番号 CONFIRM`。利用停止とは別の処理です。

## 会員管理コマンド

| 命令 | 用途 |
|---|---|
| MEMBERS / MEMBER 番号 / RIGHTS 番号 | 一覧・詳細・権限 |
| LEVEL 番号 0..5 | 会員レベル |
| GROUP / UNGROUP 番号 1..100 | グループ追加・除外 |
| GROUPMOVE 元 先 CONFIRM | 全体の所属グループ移し替え |
| SUSPEND / RESUME 番号 | 利用停止・解除 |
| DELETEUSER 番号 CONFIRM | 退会・削除扱い、IDとログイン名は永久予約 |
| RESETPASS 番号 | 新パスワードを非表示入力で再設定 |
| MEMBERPROFILE 番号 項目 値 | 他会員プロフィール編集 |

削除は履歴上の投稿者番号を詰め直す操作ではありません。容量回収では終了会員の古い認証・プロフィール記録を整理できますが、元バックアップには残ります。[保守](backup-recovery.md)を参照してください。
