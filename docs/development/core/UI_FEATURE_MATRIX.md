# 端末UIと共通機能の対応表

基準はVS統合コミット `09dc9ec0989f5758224a104cfc57325c63bde774`。表にはソースで確認した入口を記載する。接続試験とパッケージの判定は後記の検証記録に従う。[Issue #54](https://github.com/matsuotakeshi/liner-notes/issues/54)

| UI | 掲示板の表示と入口 | UI切替 | TOPIC | 一行会議 | メール・保管庫 | Door |
| --- | --- | --- | --- | --- | --- | --- |
| RTBBS | **今回変更**。B → ボード / L / LIST。NOTES と TREE は明示的な関連表示 | **今回メニュー追加**。主画面 UI | **今回追加**。主画面 TOPIC | 主画面 LINECONF | 主画面 M/Mail、F/Vault | GAME / GAMES |
| MMM | HyperNotes、主画面 2 / open | **今回メニュー追加**。主画面 UI | **今回追加**。主画面 TOPIC | 主画面 talk | 1 Mail、3 Filer | 主画面 6 / games |
| MASH | HyperNotes、主画面 2 / open | **今回メニュー追加**。主画面 UI | **今回追加**。主画面 TOPIC | 主画面 14 / talk | 1 Mail、5 Library | 6 → 4 |
| KTBBS | フラット、R → ボード → L。番号選択は表示済みID、`#ID` は現在ボードで再認可 | **今回メニュー追加**。主画面 UI | **今回追加**。! → TOPIC | ! → talk | M / F | ! → games |
| BIG-Model | フラット、B → SIG → R | **今回メニュー追加**。主画面 UI | **今回追加**。主画面 TOPIC | + → talk | M / P | + → games |
| WANI-BBS | 通常・マルチノーツ、b → ボード | **今回選択一覧を共通化**。主画面 UI | 主画面 topic | talk / LN TALK | mail / p | g |
| NET-COCK | フラット、BREAD → ボード → A/L | **今回メニュー追加**。主画面 UI | EXCOMの案内からTOPIC | EXCOMの案内からTALK | MREAD / PDS | GAMES |
| VS | 通常はフラット、B → L / R。ニュースは番号連続・関連連続 | **今回メニュー追加**。主画面 UI | TOPIC | TALK | M / F | GAMES |
| WebUI | 現行表示、ブラウザ用の掲示板 | 対象外。端末選択は端末接続の設定 | **今回追加**。TOPIC伝言板 | 一行会議 | 現行の共通Core対応画面 | 通信方式・機能によって端末を利用 |

認証済みのMMM系アダプターには `LN` の拡張を追加する。主画面では署名、プロフィール、接続者一覧、SYSOP呼び出し、電報受信設定、一行会議、Door、認可済みの管理メニューに進める。メール画面では `LN FILES`、`LN UPLOAD`、`LN RESUME`、`LN GET`、`LN GETALL`、保管庫では `LN FIND`、`LN ADD`、`LN BATCH`、`LN RESUME` を利用する。チャット停止局では関連項目を表示しない。入力・転送・Doorを終了してから操作する。

| 共通処理 | 接続先と運用上の境界 |
| --- | --- |
| 記事・検索・既読 | Article CoreとTraversal Core。返信の親ID・ルートIDを維持。RAWのNOTES、返信ツリー、Webコネクタの構造化レスポンスを維持 |
| TOPIC | Conference CoreのTOPIC操作。閲覧・投稿・削除・既読は同じ会員識別・認可を通る |
| 一行会議 | Conference Coreの会議室操作。TOPICと履歴・既読位置を共有しない |
| 一行ボード | **未実装・将来対応**。VSのボード種別、権限、ニュースポインタとの設計が必要 |
| 電報・接続一覧・呼び出し | Chat Coreと会員の受信設定。個別電文の宛先は既存の世代照合を使う画面に従う |
| メール・添付・再開 | Mail Coreと既存の転送エンジン。端末・サービス・転送方式の対応範囲に従う |
| 保管庫・一括選択・再開 | Vault、Batch、Transferの各Core。実行時に再認可 |
| 署名・会員設定 | Member Coreと既存設定。パーソナルファイルなど接続中のみの機能はUIに従う |
| Door・管理 | 既存Door Coreと管理操作。ボードや会員の権限はCoreで検査 |
| UI選択 | 登録順と設定を参照して選択肢を生成。FIXEDでは選択入口を表示しない。番号指定と識別子指定が同じ認可を利用 |
| 旧クライアント固有操作 | ジャンプレスと旧マクロの完全互換は未実装。WANI-BBSの書き込み条件等は個別仕様を参照 |

## 設計・検証

HyperNotesは共通の内部構造とする。フラット、HyperNotes、マルチノーツ、関連連続の選択はUIのプレゼンテーションに属する。記事データの移行は不要。端末UIの上限は16、最大識別子長は31、許可一覧はカンマ込み511バイトで、会員の希望は従来と同じ識別子で保存する。

共通Coreに機能を追加するPRでは、既存画面の入口、日英HELP、WebUIへの適用可否、機能対応表、相互運用試験を併せて確認する。入口の追加を保留する場合は理由と継続課題を記録する。

VSの原典は [VS_REFERENCE.md](VS_REFERENCE.md) の3件すべてのSHA-256と一致した。#225の差分履歴に原典アーカイブ・実行ファイル・リソース・メニュー・HELP素材は含まれていない。VSの統合後検証と新しい基盤の検証は対象SHAを分けて記録する。

- #225 Hosted CIは成功。統合後の [Multi-OS Native CI](https://github.com/matsuotakeshi/liner-notes/actions/runs/46027258356) と [FreeBSD 15 Package CI](https://github.com/matsuotakeshi/liner-notes/actions/runs/46027258351) は別途結果を確認する。
- 16枠、511バイトの許可一覧、範囲外ビット、16番目の保存と復元、遅延確保・解放、確保失敗時の旧UI保持はローカルのUI寿命試験で確認した。
- Coreの番号順移動、非表示記事の除外、ボードACLとセッション依存境界はローカル試験で確認した。実Telnetでの8 UI切替、RTBBS・KTBBSのフラット表示、RAWの親記事一覧、対話UI間のTOPIC共有は回帰試験に追加した。Web側も既存Connectorの試験に、未認証・削除権限の拒否と端末から送った伝言の閲覧・既読を追加した。OS別の実行結果は未確認。

既存の正式版は `v0.75`。次期候補は `v0.76`。基盤整備の最終main SHAに対するHosted・Native・内蔵SSH・更新試験とRelease Setの照合が済むまで、リリース判定は保留とする。

0.76候補の版表記は本体、Ports、RPM、DEB、統合マニュアルで一致した。Solaris IPSの版は既存のマニフェストから生成される。マニュアルの生成、ZIP、2,856件の内部リンク、検索8ケースはローカルで成功した。Connectorの状態応答・境界試験も成功した。配布マニフェストの単体試験は7件中6件が成功し、シンボリックリンク逸脱ケースはローカルのWindowsがリンク作成を拒否したため未検証とする。

リリース判定の残りは、実接続中心の全UI横断回帰、原典アーカイブの成果物側での混入確認、候補SHAの必須Actions・全OSパッケージ・更新試験・Release Set。VSの一行ボードは共通Core未実装として残っている。新しいタグの公開や配備を実行するワークフローの有無は、タグ確定時に確認する。
