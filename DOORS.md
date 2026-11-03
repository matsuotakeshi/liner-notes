# FreeBSD外部ゲーム

`--doors /絶対パス/doors.conf`、rc.dでは `liner_notes_doors` で設定ファイルを指定します。省略時は登録ゲームなしです。変更は再起動で反映します。

```text
# 番号|最低会員レベル|制限秒数|実行ファイル絶対パス|作業ディレクトリ絶対パス|表示名
1|0|600|/usr/local/share/liner-notes/scripts/door_guess.py|/var/db/liner-notes/games|数当て
```

作業ディレクトリを先に作成し、サービスのユーザーに必要な権限を与えます。付属の数当てゲームは起動確認用です。ゲームごとに実行ファイルと作業ディレクトリを指定してください。設定行は最大32件、番号は1〜86400、最低レベルは0〜5、制限時間は1〜86400秒です。ゲストは起動できません。

メニュー表示中の `GAME` / `GAMES` で専用ゲームメニューに移り、利用可能なゲームを表示します。番号だけで起動し、終了・制限時間・Ctrl-]で同じゲーム一覧へ戻ります。Enter / L / ? は一覧再表示、0 / EXIT / MENU は主画面へ戻ります。ゲーム番号が記事番号として扱われることはありません。満員の場合はその旨を表示します。登録なし・会員レベル不足で一覧が空の場合も案内を表示します。

`DOOR 番号` による直接起動も維持し、この場合は元のBBS画面へ戻ります。RAWとWebコネクタの `GAMES` は従来どおり機械向け一覧だけを返し、ゲームメニューへは移りません。Telnetクライアント自身がCtrl-]を捕捉する場合は、そのクライアントの送信機能を使ってください。接続切断や権限剥奪ではゲームのプロセス群を終了します。CONNECTIONSにはDOORと表示します。

実行は登録済みのファイルだけです。利用者の文字列をシェルコマンドへ展開しません。引数が必要なゲームは設置者が固定引数のラッパーを用意してください。rootで動くruntimeからのゲーム起動は拒否します。

ゲームはBBSと同じOSユーザーで実行されます。OSの隔離環境ではないため、設置者が信頼するプログラムを使ってください。通信ソケット・BBSデータ用ファイル記述子は子へ渡しません。同時起動枠を設定した場合は、その枠のロックFDだけを追加で子へ継承します。環境変数は以下に限定します。

- `LINER_USER_ID`, `LINER_USER`: 会員番号とログイン名
- `COLUMNS`, `LINES`: 起動時の端末サイズ
- `TERM=xterm`, `LANG=C.UTF-8`, `HOME=作業ディレクトリ`
- `PATH=/usr/local/bin:/usr/bin:/bin`
- `LINER_DOOR_ID`: 設定のゲーム番号
- `LINER_DOOR_NODE`: 取得した起動枠番号（1〜N）。枠の設定がなければ0
- `LINER_DOOR_DROP_DIR`: 今回の非公開ドロップファイル用ディレクトリの絶対パス。ドロップファイルを使わない場合は空

TelnetのNAWS通知によるサイズ変更は、Door実行中もPTYへ反映します。
BBSと同じ幅20〜240列・高さ5〜100行を使い、範囲外（0を含む）は各方向を
80列／24行へ戻します。NAWS未通知の場合は現在の会員設定・既定サイズを保持します。
不完全な通知は完結するまで反映せず、長さの不正な通知や交渉されていない通知は無視します。
NAWSの取り消し（WONT）で値が0に戻った場合もBBSと同じ既定サイズへ戻します。
同一サイズへの変更でTIOCSWINSZを繰り返さず、PTYの現在値と異なる場合だけ更新します。

`COLUMNS`と`LINES`は起動時の値で、実行中には更新されません。Door側は
`TIOCGWINSZ`、Pythonの`os.get_terminal_size()`などで現在のPTYサイズを取得し、
必要に応じて`SIGWINCH`で再描画してください。StarRoute固有の処理はBBS本体に含みません。
WindowsではネイティブDoorと同様にリサイズも未対応です。

標準入出力・エラー出力はPTYです。即時キー入力が必要なゲームは自身でPTYをrawモードにします。実行時間制限とCPU時間制限を設定します。BBSデータを操作する専用API、DOS実行・FOSSILエミュレーション、DOOR.SYS生成、得点の会員情報への取り込みは含みません。

StarRoute Tradersを単独BBSの新規宇宙として導入する場合は、
[本番導入手順](STARROUTE.md)と別配布の`starroute-liner-standalone-20261102.zip`を使います。
既存のDoor機構と会員IDを利用し、ゲーム用の追加待受ポートやBBSデータ形式の変更は不要です。

## 汎用ドロップファイルと同時起動枠

既存の6項目の設定は変更不要です。必要なゲームだけ7項目目を追加します。

```text
# 旧設定: ドロップファイルなし、ゲーム別の枠制限なし
1|0|3600|/usr/local/libexec/starroute-door|/var/db/starroute-traders|StarRoute Traders
# DORINFO1.DEFを生成し、1人ずつ起動
2|0|3600|/usr/local/share/liner-notes/scripts/door_clans.sh|/var/db/liner-notes/games/clans|The Clans|dorinfo1:1
# ファイル生成なし、最大4人
3|0|600|/usr/local/libexec/another-door|/var/db/liner-notes/games/another|Another game|none:4
```

7項目目は `dorinfo1:N` または `none:N`（N=1〜32）です。未知の指定・0・範囲外は起動時の設定エラーになります。既存StarRouteの6項目設定に追加する必要はありません。

枠は作業ディレクトリ内の `.liner-door-slot-N` を `flock` で管理します。同じOSユーザー・同じローカル作業ディレクトリを使う複数runtimeでも排他します。同じ世界を指す設定は同じ上限に揃えてください。異なる世界は作業ディレクトリを分けます。ロックファイルは通常運用中に削除・置換しません。ファイルが残っていることと使用中であることは異なります。ゲーム自身のメンテナンス・resetを管理者が直接実行する場合、この起動枠は自動適用されません。必ずゲームを止めてから行います。

起動ごとに作業ディレクトリ直下へ `.liner-door-run-XXXXXX`（0700）を作り、`DORINFO1.DEF`（0600、ASCII、CRLF、12行）を保存します。システム名 `Liner notes`、SYSOP名 `SYSOP`、COM0、0 BAUD,N,8,1、予約行0、利用者ID `LN` + 9桁会員番号、空の姓・所在地、ANSI=1、会員レベル0〜5、残り分数相当（設定秒数を切り上げ）を渡します。実時間の制限はBBS側の秒数・会員接続制限が優先です。個人プロフィール・パスワード・管理者認証情報は渡しません。ゲーム内の管理者権限は自動付与しません。

固定IDはハンドル変更や日本語名の切り詰めで別人になることを避けるためです。複数BBSで会員番号が重なる共有世界の本人識別までは提供しません。その場合はStarRouteのようにBBS識別子も扱う専用連携を利用してください。

通常終了・強制復帰・タイムアウト・回線切断時はSIGHUPで端末切断を知らせ、最大250msの猶予後に残るプロセス群をSIGKILLします。BBSのイベントループは待機で停止しません。主プロセスの回収とプロセス群の消滅を確認してからドロップファイル削除・枠解放を行います。子孫が別セッションへ離脱するdaemon型ゲームは対象外です。runtime自体のSIGKILL・OS停止では自動清掃を保証しません。子へ継承したロックはそのFDが閉じられるまで残ります。異常停止後の不要なrunディレクトリは関連プロセスの停止を確認して管理者が整理します。ゲームが追加ファイルを作った場合は再帰削除せず残します。

## The Clansネイティブ版の導入

今回確認したソースは [SynchronetBBS/sbbs](https://github.com/SynchronetBBS/sbbs/tree/442843d7aae805670e59d103b326757598feb400/src/doors/clans-src) の `442843d7aae805670e59d103b326757598feb400`（Clans 0.97b2）です。[上流の導入説明](https://github.com/SynchronetBBS/sbbs/blob/442843d7aae805670e59d103b326757598feb400/src/doors/clans-src/INSTALL)も参照してください。ゲーム本体はLiner notes配布物に同梱しません。

まず一般ユーザーで新しいビルド領域へ取得・ビルドします（FreeBSDではgmake）。

```sh
git clone --filter=blob:none --sparse https://github.com/SynchronetBBS/sbbs.git clans-source
cd clans-source
git sparse-checkout set src/doors/clans-src src/odoors
git checkout 442843d7aae805670e59d103b326757598feb400
cd src/doors/clans-src
gmake -j4 game
gmake install INSTALLDIR="$HOME/clans-stage"
```

以下は設置者が新規世界へ導入するときの例です。既存世界へ上書き・resetしないでください。BBSをこの新版へ更新する通常のバックアップ・切替手順を先に行い、実際のサービス実行ユーザーが `liner_notes` であることを確認します。

```sh
# root: 未作成の新規ディレクトリを作り、ステージのゲーム一式をコピー
install -d -o liner_notes -g liner_notes -m 750 /var/db/liner-notes/games
mkdir /var/db/liner-notes/games/clans
# /absolute/path/clans-stage は上で作った実際のパスに置き換える
cp -R /absolute/path/clans-stage/. /var/db/liner-notes/games/clans/
chown -R liner_notes:liner_notes /var/db/liner-notes/games/clans
chmod 750 /var/db/liner-notes/games/clans
```

TTYのあるSSH接続等でゲーム専用初期設定を開きます。

```sh
su -m liner_notes -c 'cd /var/db/liner-notes/games/clans && ./config'
su -m liner_notes -c 'cd /var/db/liner-notes/games/clans && ./reset'
```

`config` でSYSOP名・BBS名を設定し、単独運用ならInterBBSを無効にしてSave Config。**新規世界だけ** `reset` で開始日・村名等を決め、Reset local gameを選びます。すでにプレイしている世界へresetを実行するとゲームデータを失います。

更新したports定義は `scripts/door_clans.sh` を `/usr/local/share/liner-notes/scripts/` に配置します。0.72の正式配布にはこのラッパーを収録しています。ソース版は `make` でこのruntimeをビルドし、同じスクリプトを実行権限付きで配置します。`doors.conf` に上のThe Clans行を追加し、既存StarRoute行は維持します。`liner_notes_doors` がその設定ファイルを参照していることを確認したうえで、設置者が計画したタイミングでBBSを再起動してください。利用者は `GAME` → `2` で起動できます。番号2は例なので既存の番号と重複させないでください。

ラッパーは `./clans /N枠番号 /D相対パス/DORINFO1.DEF /CP437UTF8` を実行します。OpenDoorsの `info_path[60]` 制限に合わせ、短い相対ファイル名を明示します。UTF-8端末向けにゲームの変換オプションを使います。`/L` はBBS会員情報を読み込まないので使いません。

日次保守 `./clans /M` はゲーム上流の運用仕様です。実行前にプレイ中のゲームを止める手順を組み、今回の導入で無条件にcronへ追加しないでください。

The Clansは `online.flg` も独自に使うため `dorinfo1:1` で運用します。通常終了・Ctrl-]・回線切断ではゲーム自身のSIGHUP処理による解除を確認しています。OS停止やSIGKILLでその処理ができなかった場合、このファイルが残ることがあります。ゲーム・保守処理が動いていないことを確認してから管理者が回復させます。BBS本体はゲーム固有ファイルを無条件に削除しません。

## 既存の /usr/local/clans を使う場合

既存StarRouteの行を残し、未使用の番号で次をdoors.confへ追加します。

```text
2|0|3600|/usr/local/share/liner-notes/scripts/door_clans.sh|/usr/local/clans|The Clans|dorinfo1:1
```

`clans`が短いシェルスクリプトでも、同じディレクトリの
`clans.freebsd.amd64.opt`を実行する構成なら利用できます。
既存世界で `reset` は実行しません。configはSYSOP/BBS名と単独運用時のInterBBS無効を確認します。
サービス所有者がゲームディレクトリへ一時ファイルを作成でき、既存ゲームデータを更新できる必要があります。
root所有0755のディレクトリやwww所有0644のデータはliner_notesから書き込めません。

次は本番接続で確認した構成の例です。ゲームを停止し、実際の所有者・データを確認したうえで使います。
実行ユーザーが異なる環境や、他サービスと共有している世界へ無条件に適用しないでください。

```sh
cd /usr/local/clans &&
chgrp liner_notes . outbound &&
chmod g+rwx . outbound &&
chgrp liner_notes *.dat scores.ans scores.asc today.asc news.txt &&
chmod g+rw *.dat scores.ans scores.asc today.asc news.txt
```

ゲーム本体・ラッパー・設定を777にする必要はありません。新しいゲームデータはゲームを動かす
サービスユーザーの所有になります。権限変更だけならBBSの再起動は不要ですが、doors.confの
追加はBBS再起動で反映します。GAME→番号→終了→一覧復帰と、既存StarRouteの起動を確認します。
