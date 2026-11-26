# 2026年11月 全体監査の基準と範囲

基準は `ae0524fa74601030ecac390bb76dd245aeabf15c`。このコミットは #211 のマージで、第一親は #210 統合後の `0e7173a`、第二親は `80fe2dd`。独立した作業ツリーに固定した。以下は修正前の調査記録である。

## 全体の棚卸し

`git ls-files -z` で1,334ファイルを列挙した。src 231、tests 352、docs 283、evidence 78、scripts 52、ci 45、help 44、.github 33、webui 28、ports 21、linux 16、menus 14、debian 10、solaris 9、third_party 3、rc.d 1、その他はルート直下。全293個のCソース・ヘッダ・incのローカルincludeを呼び元相対とsrcで解決し、重複を除く1,051の依存辺、解決不能0件。全389 PythonファイルをASTとして読み、呼び元とモジュールの4,035組を抽出した。全workflowのYAMLを読み、計49ジョブのトリガーとrunnerを照合した。履歴文書、生成済みフィールド表、同梱端末ライブラリ、過去ログも棚卸しの対象に含む。各行を人手査読したという意味ではない。

|領域|実際に追った経路・証跡|限界|
|---|---|---|
|A 認証・権限|members.c のPBKDF2・固定時間比較・pending/deleted、authentication_core.c の停止判定、ssh_transport.c の署名済み鍵とチャネル制限、session_core_submit の本人・世代の上書き、article_core/mail_core/vault_worker の照合。webui/app.py のCookie/CSRF/Origin、Socket・HTTPの寿命とDOMテキスト出力。Doorのpeer UID、Brokerの単発券、link_store.py のEd25519署名・チェックポイントを照合。|導入済み環境の鍵・ラベル・外部設定は調査対象外。|
|B Cの安全性|16翻訳単位をUCRT64 GCCの `-std=c11 -Isrc -fanalyzer -Wall -Wextra -c` で個別解析。door_core, door_protocol, mmm_input, terminal_pager, telnet, classic_input, authentication_form, session_core, transfer_line, conference_core, chat_core, list_navigation_core, message_flow_core, session_ui, session_web_view, session_web_update はすべて完走し、診断0件。文字列の容量、親子プロセスのFD、確保失敗とSQLite後始末を別途静的確認。|POSIX固有分岐にこのコンパイラの結果を外挿しない。ASan/UBSanおよび任意のメモリ不足の網羅試験は未実施。|
|C CoreとUI|session_ui.c で切替許可、秘密入力・転送中の禁止、状態解放と結果配達を確認。RTBBS・MMM/MASHは同一のsession_coreを通る。Webのtyped updateはサーバー側で要求を再構成する。私有UIヘッダを直接includeするCore実装は見つからず、既存test_session_boundaries.pyの9件が合格。|古いコネクタとの互換分岐には表示テキストの判別が残る（AUD-008）。外部リンク管理は別サービスのソケット境界を持つ。|
|D DB・保存|storage_sqlite.c の開始・sync・rollback・close、storage_records.inc・normalized_fields、logical export/import、maintenanceの隔離復元、MMM scanlistのsavepoint、conferenceの単調な既読位置を照合。登録申請の別ストア保存失敗はaccount reservedとして明示される。|バックアップ対象選択の実験は一時データのみ。全OSでの媒体障害・電源断の実験は未実施。|
|E 並行性|session_core_matches、vault_workerのslot/世代、ジョブ回収、Webのasyncio lockとイベント購読・認証・転送枠・終了処理を追跡。|Cのスレッドスケジュールをすべて検証したわけではない。|
|F 通信・転送・Door|Telnet/SSH→Session、サイズ変更→PTY、Doorのslot・process group・終了回収、隔離launcher/peer資格情報、ZMODEM control/dataのsocketpair、終了受領、Webアップロードの入出力と再開情報を確認。|実モデムや本番Doorを使用していない。X/Y系と異機種での動作は既存の回帰試験に依存する。|
|G 移植・パッケージ|Makefileの翻訳単位とヘッダ、FreeBSD @sample、linux/rpm と linux/deb のmanifestと導入スクリプト、Solaris IPSの設定所有境界とSMF試験を確認。Solarisのlogin_tty、FreeBSD/Linuxと異なるpeer UID取得経路を確認。|今回の変更のネイティブ実行・パッケージ更新結果はマージ後に別途取得。|
|H CI・試験|全self-hostedジョブのifとイベント、scope判定・main/PRの責任分担を確認。Solarisのmake・Web・リンク・転送は別ステップだがコマンドごとの所要時間が残らない。|リポジトリ外のrunner group/Environment保護設定、実際の本番権限は確認していない。|

監査時点の基準SHAには、FreeBSD/Rocky/Debianのパッケージ生成とRelease Set（run 45874697785）の成功がある。Linux互換性（45874386948）とMulti-OS（45874386824）は確認時に実行中、FreeBSD隔離更新（45874697715）は待機中。これらは修正版の証跡には使わない。ネイティブCIは信頼済みmain専用である。

Windowsで利用できた解析器はGCC。WSLは導入されていない。既存テストの適用範囲と実行結果を区別した。未検出を安全性の保証とせず、実機でのセッション失効、負荷、DB復旧、パッケージ・Solarisの日次試験は修正報告側で追跡する。
