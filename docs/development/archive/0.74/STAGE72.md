# Liner notes 0.72

2026-11-03。WebUI・転送改善・汎用Door連携を正式配布に統合しました。

- WebUI: 記事、メールと直接添付、保管庫、チャット、プロフィール、管理、補助端末。
  ログイン導線、パンくず、投稿先選択、再開候補、外部HTML/メッセージに対応。
- 大容量転送: 64KiBコネクタ、非同期登録と進捗、失敗理由・再開候補の表示。
- GAME/GAMESの独立メニュー、番号起動、終了後の一覧復帰。
- Door: 既存6項目設定を維持し、任意のDORINFO1.DEF・同時起動枠・異常終了処理を追加。
  Telnet NAWSをPTYへ伝達。The Clans用ラッパーを収録し、既存StarRouteを保持。
- Webテンプレート配置権限を修正。ファイル読み取りエラーとBBS接続障害を区別。
- GPL-3.0-onlyと第三者ライセンスを明示。独自lrzsz ports/パッチを配布から除外。
- FreeBSD portsのWEBUIオプション（既定ON）、依存、専用ユーザー、rc.d、sample配置。

## 更新

[UPGRADING.md](UPGRADING.md)を参照。DBの自動変換、ゲームの初期化、
設定の上書き、サービスの自動再起動は行いません。0.71またはレビュー版からは
本体とWebUIを同時に更新し、編集したテンプレートと新sampleを比較してください。
本体の配置は `/usr/local/sbin/liner-notes`、Webは `/usr/local/sbin/liner-notes-web`。

## 検証と制限

今回のリリース検証は `evidence/stage72/verification.json` に記録します。
機能の検証記録は [WEBUI_REVIEW_VERIFICATION.md](WEBUI_REVIEW_VERIFICATION.md)、
[DOOR_GENERIC_VERIFICATION.md](DOOR_GENERIC_VERIFICATION.md)も参照してください。
約160MBの実ファイル転送は検証済みですが、本番HTTPSでの速度保証、1GiB×16、
最大規模の長期負荷、Clans全シナリオの検証ではありません。
Webの一部帳票は文字表示です。全操作を専用画面化したという意味ではありません。
The Clans/StarRoute本体、DOSエミュレーター、lrzsz本体は同梱しません。
