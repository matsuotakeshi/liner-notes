# Door実行中のTelnet端末サイズ変更

## 取り込み元と比較

- StarRoute Traders基準コミット: `225dd31a61871e65242558028b0806589743ac0d`
- 参照: `verification/phase5/freebsd-starroute-resize.patch`、変更前後のSHA-256、
  `scripts/phase5_telnet_demo.py`、`PHASE5_COMPLETE.md`。
- Liner notesの取り込み前HEAD: `edd3727`、作業ブランチ`web-ui`。
  未コミットのWebUI・起動処理修正を含む現行作業ツリーを複製して検証。
- `doors.c/h`の正規化した変更前ハッシュはStarRoute側の基準と一致。
  `runtime.c`はその後の修正を含むため、無条件にパッチを適用せずDoor処理だけを変更。
- 専用コピー向けの`prepare_resize_copy.py`は実行していない。

## 採用した挙動

本体の変更は`src/doors.h`、`src/doors.c`、`src/runtime.c`。
TelnetのNAWS値が変わった場合だけ、既存の`session_dimensions()`で寸法を
確定して`door_resize()`へ渡す。PTYは`TIOCGWINSZ`で読み、違う場合だけ
`TIOCSWINSZ`で更新する。サイズ変更後にDoor入力・出力の処理を継続する。
Door終了が決まった場合はセッション寸法を保存してPTYの更新だけを省く。
PTYの終了競合でもBBS接続は
サイズ変更失敗だけを理由に切断しない。

StarRoute検証用の高さ12〜120行は採用せず、既存のLiner notes仕様を維持した。

| 入力 | 動作 |
| --- | --- |
| 幅20〜240列・高さ5〜100行 | そのまま反映 |
| 範囲外または0 | 該当する方向だけ80列／24行へ戻す |
| NAWS未通知 | 会員設定・現在のセッション寸法を保持 |
| 同一サイズ | 余分なTIOCSWINSZを実行しない |
| 分割されたNAWS | 完結してから反映 |
| 長さ不正・未交渉のNAWS | 既存Telnetデコーダで無視 |
| WONTによる通知取り消し | 値が0に戻ると既定寸法へ戻す |

`COLUMNS/LINES`は従来どおり起動時の環境変数。実行中のサイズ取得はDoor側が
PTYを照会する。認証情報、Door設定、会員データ、ゲームAPIは変更していない。
Windowsは0を返す未対応分岐であり、WindowsのPTY動作を実証する変更ではない。

## 検証方法

FreeBSD 15.1-RELEASE/amd64、clang 19.1.7、Python 3.12.15。
新規領域`/home/takeshi/liner-notes-door-resize-20261102`を使用。
全テストはループバック待受と一時データで実行する。
既存の`liner-notes-stage70-work`、StarRouteの`final3`、本番のサービス・設定・
データは更新しない。StarRouteは`final3/source`をさらに複製し、デモのゲームDB・
会員・TLS証明書・認証情報を新規作成する。

未修正版では90×28でDoor起動後、80×24のNAWSを送信しても
`SIZE 90 28 SIGNALS 0`のままになることを再現した。

追加の`tests/test_door_resize.py`は、PythonのSIGWINCH対応PTYプログラムと
独立した`stty size`プログラムを使用する。通知の境界値と形式、同時会員間の分離、
認証済みID、環境変数、終了・再入場・異常終了・Ctrl-]・切断・時間切れ、
通常BBS操作への復帰を実際のTelnet接続で確認する。
`make test-stage65`および`make check`にも追加した。

実行コマンド:

```sh
python3 tests/test_door_resize.py ./runtime
make test check test-stage67 test-stage68 test-stage69 test-stage70
python3 tests/test_web_connector.py ./runtime
python3 tests/test_web_http.py ./runtime ./vaultctl
python3.12 starroute-source/scripts/phase5_telnet_demo.py \
  --directory /home/takeshi/liner-notes-door-resize-20261102/starroute-demo \
  --liner /home/takeshi/liner-notes-door-resize-20261102
```

Web HTTP試験にはaiohttpを導入済みのPythonを使用する。
StarRouteの430件の単体試験をLiner notesの回帰試験件数には含めない。
本番への配置・再起動、GitHub公開、リリース作成は今回の作業には含まれない。

## 最終候補の確認結果

終了キーとNAWSが同じ入力に届く境界を追加し、セッション寸法を保持する最終候補を
専用領域の`final-check`へさらに複製してビルド・再検証した。
後述の広範囲試験は最初の候補に対する結果であり、この最後の調整後は関連試験を再実行した。

| 試験 | 結果 |
| --- | --- |
| `make -j2 all` | 成功。警告をエラー扱いにした既存ビルド設定 |
| `make test-stage65` | 成功。新設NAWS試験と既存Door・端末・メニュー・DB等の8コマンド |
| `test_telnet.py` / `test_telnet_client.py` | 成功。実Telnet、パスワード非表示、通常BBS操作 |
| `test_web_connector.py` / `test_web_http.py` | 成功。Webコネクタ・HTTP・認証・ファイル・PTY連携 |
| StarRoute Phase 5の実Telnetデモ | 成功。新規の2つのBBS・ゲームDBで実施 |

StarRouteの結果JSONは`naws_resize: true`、日本語・英語、売買・移動・帰還・再入場、
共有市場・投資・航路・WHO・切断後の資産保持がすべて成功、`integrity: "ok"`。
StarRoute側がPTYの現在寸法を再取得する実装と組み合わせた結果である。

最終候補の本体3ファイル・試験・Makefile・DOORS.mdのSHA-256と、
ローカルで取り込むファイルのSHA-256が一致することも確認した。
Linux実機およびWindowsのPTYリサイズは今回検証していない。
ZMODEMの大容量転送等、任意実行の追加負荷試験も今回の実行対象には含めていない。

証拠は開発作業領域の`../../build/door-resize-evidence/`へ保存した。
`final-check-final-regression.log`、`final-check-starroute-demo-final-result.json`、
`final-source-hashes.json`が最終候補の記録。
検証サーバー側の原本は専用領域の`final-check/final-regression.log`、
`final-check/starroute-demo-final/result.json`に残している。

## 既存試験で判明した問題

最初の候補に対する広範囲実行は120試験コマンド中119成功、下記の1失敗。
最初のmake実行で失敗までに56コマンドが成功し、継続実行の63コマンドはすべて成功した。
これは試験コマンドの実行回数であり、内部のアサーション数や重複を除いた試験数ではない。
`test`、`check`の各依存ターゲットとstage67〜70を対象とし、
`remaining-results.json`に継続分の全終了コードを記録している。

`make test check test-stage67 test-stage68 test-stage69 test-stage70`は、
`tests/test_i18n.py`のエラー文言対応表検査で停止した。
未コミットのWebUI実装が使う`ERR connector editor state`について、
`src/ui.c`の翻訳対応表に項目がない。

これは取り込み前にも同じ条件で失敗する。対象の`session.c`、`ui.c`、`board.c`、
翻訳ファイルが取り込み前のSHA-256と一致し、`runtime.c`のエラー文言集合も
変わっていないことを確認した。検出した対応表不足は上記の1項目。
Door修正とは分離し、今回の差分で翻訳・WebUI実装を変更していない。

残りの試験はMakefileの実際のレシピから順に取り出し、個別の終了コードを記録して
継続した。`test_i18n.py`を成功扱いにしたり、検査を無効化したりしていない。
この失敗の後に同ファイルで実行する言語切替のソケット試験は未実行である。
そのため、この作業を「make check全件成功」とは扱わない。
