# 汎用Door連携・ゲームメニューの実装記録

2026-11-02。設計確認に続き、ユーザーから「実装・検証まで進める」の回答を受けて実施。

## 実装の境界（Codex / Workへの引き継ぎ）

- `src/session.c,h`: ゲームメニューの状態を追加。対話画面のGAME/GAMESから番号起動、終了後一覧、0で主画面。RAWとWebコネクタのGAMESは従来の一覧応答を維持。
- `src/doors.c`: 既存6フィールド設定に任意の7フィールドを追加。`dorinfo1:N` / `none:N`、N=1〜32。ゲーム作業ディレクトリ単位のflockと起動ごとの非公開ドロップディレクトリを管理。
- ドロップファイルは標準DORINFOの12行、CRLF、0600。会員番号から固定ASCII IDを生成し、私的プロフィールやパスワードを渡さない。
- `src/runtime.c` / `src/ui.c` / 日英メッセージ: 満員の表示、ゲームメニュー案内、空の一覧の説明。
- `scripts/door_clans.sh`: The Clans固有の引数、OpenDoorsの60バイトパス制限、UTF-8化を吸収。本体にClans用API・ゲーム内処理・ゲームファイル削除を入れない。
- 終了時はSIGHUPで250msの猶予を与え、その後残るプロセス群をSIGKILL。主プロセスと同一プロセス群の消滅後に枠を解放。待機はイベントループを止めない。
- ports・Linuxインストーラにラッパー配置を追加。既存設定・会員データの変換は不要。

WindowsのネイティブPTY、DOS/FOSSIL、DOOR.SYS生成、異なるBBS間でのIDの共通化は含まない。外部ゲームの信頼性・データ復旧はゲーム側の責任も残る。The Clansは1枠で運用する。OS停止やSIGKILLで残ったゲーム独自の `online.flg` をBBSが勝手に削除しない。

## 検証環境と分離

- FreeBSD 15.1-RELEASE / amd64、Python 3.12.15。
- `/home/takeshi/liner-notes-doors-generic-20261102` の独立コピー。
- BBSテストはloopbackの動的ポート・一時データ。The Clansは新規にconfig/resetした検証世界を、各試験ごとにさらに複製。
- 上流Clans: SynchronetBBS/sbbs commit `442843d7aae805670e59d103b326757598feb400`、Clans 0.97b2。ゲームソース改変なし。
- StarRouteは既存の検証キットで新規宇宙を作成して実行。本番宇宙は使用しない。
- 本番Liner notes・稼働サービス・会員データ・既存設定は変更しない。

## 試験

`tests/test_doors_generic.py` は実Telnetで、番号選択、一覧復帰、権限、即時キー入力、ドロップファイルの12行と権限、同時利用者分離、別runtime間の枠共有、別ゲームの並行利用、異常終了、無反応なプロセスと子孫の切断後終了、Ctrl-]、時間切れ、シンボリックリンク拒否を確認する。

`tests/test_doors_config.py` は、不正設定、起動失敗後の枠・ファイル回収、再試行、ファイル生成なしの枠制限、登録ゲームなしのメニューを確認する。

`tests/test_clans_native.py ./runtime /absolute/path/to/fresh-test-world` は実Clansを使用する任意試験。認証済み会員2人の固定ID、UTF-8として妥当なANSI出力、ゲーム内の案内・参加確認操作、通常終了、Ctrl-]、切断、再入場を確認する。ゲーム全シナリオやゲーム内資産の全検証を意味しない。引数は本番世界ではなく、新規に初期化した検証専用世界にする。

既存のDoor、NAWS、通常BBS、会員権限剥奪、rc.d、Webコネクタを回帰対象にした。daemonテストはGAMES後に0で主画面へ戻る操作を追加し、新しい対話仕様に合わせた。

## 同時収録のWebUI修正

以前依頼されたテンプレート配置とエラー表示を修正。新規インストール時のumaskに依存せずディレクトリを0755で作り、WebUI所有のテンプレート等のディレクトリも修復する。web.tomlは0640、設置者の既存本文は保持する。

テンプレートの権限エラー・不正UTF-8はHTTP 500として専用の説明を返す。BBSのネットワーク接続失敗はHTTP 503を維持する。詳細はサーバーログへ記録し、ブラウザーに内部パスを返さない。

`tests/test_web_install.py` と `tests/test_web_errors.py` でumask 077、再インストール時の本文保持とディレクトリ権限修復、欠落時フォールバック、権限・文字コードエラー、回復後の表示、BBS接続失敗との区別を確認する。

導入・設定の具体例は [DOORS.md](DOORS.md) を参照。ソース更新の段階であり、本番への適用・再起動、GitHubへのcommit/releaseは実施しない。

## 実行結果

- FreeBSDの `make all`、`-Wall -Wextra -Werror` によるビルド成功。
- 汎用Doorの実Telnet試験、設定・失敗時清掃、既存Door、NAWS、権限剥奪と子孫プロセス終了の試験成功。
- The Clansネイティブ版: 実Telnetで2会員の識別、UTF-8 ANSI、参加案内・確認、通常終了、Ctrl-]、回線切断、その後の再入場を成功。試験用に上流ソースをビルドし、ゲームコードは変更していない。
- StarRoute: 既存の独立検証キットを使い、Telnetと実WebSocketのPTY接続、売買結果の永続化、2会員分離、80×24→100×30の変更、切断・再接続・BBS復帰を成功。
- `make test-web` 成功。初回のWeb connector試験で一時的な `ERR try later` が出たため同一ソースで再実行し、全項目が成功した。
- `make check` の既存回帰は分割して完了。通常の `test` 全項目、stage61、maintenance、stage63の容量・権限・読取障害試験を成功。省メモリ試験の起動待ちを下記のとおり再検証し、その後 `LINER_TEST_START_TIMEOUT=180 make test-stage64 test-stage65 test-stage66 test-stage71 test-stage67 test-stage68 test-stage69 test-stage70` が終了コード0で完了した。
- 大規模試験は最初の5秒起動待ちで失敗。対照の旧バイナリでも同じ失敗を確認。既存の大規模試験用設定 `LINER_TEST_START_TIMEOUT=180` で新旧とも成功した。
- 記事12,000件＋メール12,000件、各本文4KiBの読み出し・検索・返信・送信・既読を成功。今回のRSSは79,577,088→89,038,848バイト（増分9,461,760バイト）、旧バイナリの増分は9,641,984バイト。判定上限48MiB以内。

Clansのブラウザー内での見た目、Linuxでの実行、WindowsでのネイティブDoorは今回の実機試験に含めない。Linux用ラッパー配置はソースへ反映したが、FreeBSD上ではLinuxインストーラ自身が実行を拒否するためLinux実機の配置確認済みとは扱わない。

検証runtimeのSHA-256: `31954069583e0f8d42c7e305694982240ebb1f55f543416468fc0e8d65dc42be`。
証拠ログは開発ワークスペースの `rtbbs-port/build/doors-generic-evidence/` と、上記FreeBSD検証領域に保存。
