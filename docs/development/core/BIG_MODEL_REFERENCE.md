# BIG-Model/98 4.0系の操作資料と対応

調査対象はBIG-Model/98 Ver.4.0系。Liner Notes側の基準は `56f19e8418941b934450f916ff1d65517008b2e3`。Issue #54の選択ポリシーを継承する。

## 資料の取得状況

| 資料 | 確認範囲 |
| --- | --- |
| [Perl版プロジェクト](https://www.naoit0.com/projects/big-model/start)、[利用者向け](https://www.naoit0.com/projects/big-model/manual/user/start)、[管理者向け](https://www.naoit0.com/projects/big-model/manual/admin/start) | 公開元を確認。開発者は実機とマニュアルの照合を記録。公開リポジトリのREADMEは3.0cを基に4.0試用版の変更を加えたと説明している。 |
| [電報](https://www.naoit0.com/projects/big-model/manual/user/telegram/start)、[ACL](https://www.naoit0.com/projects/big-model/manual/admin/acl/start) | メニュー、接続先選択、電文入力、受信設定、階層とCUGの解説を確認。互換版画面と資料中の歴史的説明は区別する。 |
| [Perl版ソース](https://bitbucket.org/naoit0/big-model/) | `3d0def8` の `README.md`、CP932の `data/map.txt`、`BIGModel/Menu.pm`、`BIGModel/Telegram.pm`、`BIGModel/Mail.pm`、`BIGModel/UserSetting/` を調査。利用条件を明記したライセンスファイルを確認できなかった。コード・文言・配布データは取り込まない。 |
| [通信用語の基礎知識](https://www.wdic.org/w/TECH/BIG-MODEL) | 当時の製品を扱う二次資料。1文字コマンドと階層、ヘッダーの順序、レス管理拡張の追加欄を確認。 |
| [旧製品紹介](https://www.netcomplex.co.jp/html/bm.html) | HTTP 502。本文未確認。 |
| [BMSIM 1.51](https://www.vector.co.jp/soft/dos/net/se009852.html)、[BIG_LAN 1.0](https://www.vector.co.jp/soft/dos/net/se024544.html)、[KENres](https://vector.co.jp/soft/dos/net/se014550.html)、[OkuAuto](https://www.vector.co.jp/soft/win31/net/se019263.html) | Vectorの掲載情報を確認。配布リンクから戻ったデータはHTMLで、`bmsim151.lzh`、`big_lan1.lzh`、`kenr122.lzh`、`oa16_237.exe` の付属文書は未確認。 |
| [書誌 BN04105618](https://ci.nii.ac.jp/ncid/BN04105618) | 参照先を確認。書籍本文は未取得。 |

オリジナル4.0系の操作説明書または実機ログを直接確認した項目は現時点でない。操作表の互換性は参照した資料の範囲に限定する。

## 操作仕様とCoreの対応

| 操作 | 根拠の区分 | Liner Notesでの対応 |
| --- | --- | --- |
| 主メニューの B/C/E/H/M/N/O/P/T/U/W/X/? | 互換実装で確認。N/Xなどはメニュー名の根拠で、完全な状態遷移の根拠ではない | 独立したコマンド解釈。掲示板、チャット、終了、案内、メール、探索、保管庫、電報、端末設定、接続一覧へ接続。 |
| Bから数字で下位メニュー・ボードを選ぶ | 互換実装で確認 | カタログの階層をSIGとして表示。数字は表示したボードに紐づけ、入室、閲覧、投稿をCoreで再検査。 |
| ボードの R/W/日付、メールの R/W | 互換実装で確認 | 読む・書く・日付検索を既存Coreに接続。ページ送り・返信・戻るなどの未確認のキーはLiner Notesで定義する。 |
| NUM、R.DATE、R.TIME、SENDER、CONTENTS | 歴史的資料で確認。対象版の厳密な桁数と端末制御は未確認 | 数字を最低5桁で表示し、上位桁は保持。日時は局のTZ。返信番号を常時出す根拠はないため、レス管理拡張の専用表示は対象外。 |
| 一覧と前後移動 | フラット表示は本開発の確定仕様。原作の境界動作は未確認 | KTBBSで追加した返信込みの番号順取得を再利用。記事選択と既読確定は共通Coreの認可経路。 |
| 電報 T、掲示板・チャットの !、接続先番号と電文 | 互換実装で確認。電報マニュアルは旧版にも入口があったと説明 | Chat Coreの宛先接続IDを使用。21文字はUTF-8符号位置として扱う。原作の全角、半角、合成文字の数え方は未確認。 |
| Uの端末設定、日時仮設定、探索表、電報受信制限 | 互換実装で確認。原作の保存形式、更新時点は未確認 | 共通機能を利用可能な範囲で接続。既存ログ、局ACL、他UI固有の巡回表は書き換えない。 |
| A/D/L/Q/Z | 一部の名称のみ互換実装で確認 | 原作の自動ダウンロード、アンケート、質問専用コーナー、ハムレットを再実装しない。保管庫・検索・Doorへの入口は独自拡張として案内する。 |
| メニューと1キー入力の正確なタイミング、BigMAP | 未確認 | 行入力が必要な場面をプロンプトで示す。旧自動巡回ソフトとの完全互換を保証しない。 |

データは既存の会員・記事・返信・メール・ファイルに保存する。BIG-Modelの表示用番号やSIGを独立した業務データにしない。MMM・MASHのHyperNotes表示は維持し、RTBBS・KTBBSのフラット化は後続作業とする。
