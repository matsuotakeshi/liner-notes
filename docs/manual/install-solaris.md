# Solarisへの導入

## 次期版（未リリース）：検証済みの対応範囲

対象はOracle Solaris 11.4 CBE x86-64です。安定版0.74.1の正式対応OSには含まれません。SPARC・ARMは対象外です。ネイティブビルド、基本BBS、Door、WebUI、BBS間リンク、実ZMODEM転送の自動回帰試験が成功しています。SMFでの実サービス運用は未検証です。全機能対応や本番運用の検証完了を意味しません。

## 検証と依存関係

`Solaris Native CI`は信頼済みmainの正確なコミットだけを、既存のCIアカウントと固定ホスト鍵で取得します。独立した作業ディレクトリでビルドし、OSのパッケージ導入やサービス変更を行いません。PRでは接続を伴わないCI制御のテストを実行します。従来のFreeBSD・Rocky Linux・Ubuntuの3環境の検証は維持しています。

必要な構築環境はGNU make、64ビットC11コンパイラ（ClangまたはGCC）、SQLiteとOpenSSLの開発用ヘッダー・ライブラリ、Python 3.11以上です。読み取り専用診断ツールdoctorも含めて検証します。WebUIとBBS間リンクは、それぞれaiohttp、cryptographyを必要とします。独立したPTYアプリのサイズ検査は`/usr/gnu/bin/stty`を使用します。Solaris標準sttyには`size`オプションがないためです。ZMODEM相互運用試験はlrz/lszを使います。未導入の場合、既存の公式ソース取得・SHA-256検証helperで作業コピー内だけにビルドします。Solarisではgcc/gmakeと64ビット指定を使用し、OSのパッケージは変更しません。

CIログにはOS、コンパイラ候補、ライブラリ配置、Python、正確なソースSHAを記録します。OSパッケージは自動導入しません。WebUI・リンクのPython依存は作業コピー内の専用venvにrequirementsから導入し、既存のsystem Pythonを変更しません。インストール失敗もCI失敗として記録します。ビルド、排他寿命、SMF定義検証、基本BBS、Door、WebUI、リンク、ZMODEMの検証状態は`SOLARIS_CAPABILITIES`行で区別します。追加依存がなく実行できなかった機能を成功扱いしません。

## ビルド上のOS差

Solaris 11.4の`openpty`と`login_tty`はlibcにあり、`termios.h`で宣言されます。FreeBSD/Linux向けの`libutil`をSolarisへリンクしません。ソケットは`-lsocket -lnsl`を明示します。既存のPTYと端末サイズ伝達を維持し、端末の制御取得はSolarisの`login_tty`へ委ねます。SolarisにはBSDの`flock`がないため、専用の小さな実行ファイル`liner-notes-door-lock`へexecした子プロセスがPOSIXファイルロックを保持します。BBSの巨大な静的領域を長寿命の子へ残すと、Solarisのswap予約により後続のforkがENOMEMになるためです。BBSとDoorへ継承したpipeがすべて閉じるまで保持し、BBSの異常終了後も生存中のDoorが同じ枠を占有します。FreeBSD/Linuxは従来の`flock`を使用します。

Python補助ツールもSolarisでは独立したロック保持プロセスを使用します。転送spoolとリンク保守の排他を、同一プロセスの別FDのcloseで解除させません。spool直下の固定ファイル`.namespace-lock`は空の0600管理用ファイルです。削除・置換しないでください。symlink、hardlink、公開権限、不正な内容は拒否します。転送一覧・削除・復元はこのファイルを転送データとして扱いません。既存SunOSの通常ファイルに対するPOSIX記録ロックとは競合しますが、旧版のディレクトリロックは動作しないため、同じspoolで旧新プロセスを並行使用しないでください。FreeBSD/Linuxのflockと保存データ形式は維持します。ZMODEM再開検査の検証記録も同じ排他を使い、Solarisでは保存先ディレクトリに`.namespace-lock`を置きます。サービス所有者で実行し、親ディレクトリを他者から書込不可にしてください。`.namespace-lock`は検証記録の名前として使わず、稼働中に削除しません。

独立した作業コピーでの検証入口は次のとおりです。CIでは上記のmain検証を通った後に同じコマンドを使用します。

```sh
python3 ci/solaris_ci.py inventory
python3 ci/solaris_ci.py test
```

`test`は既存の基本BBS回帰試験、DBの別コピー変換・バックアップ・復元、Door試験を実行し、依存を満たす追加機能も試験します。失敗した試験は終了コードへ反映します。

## 配置・SMF定義の準備

`scripts/install-solaris.py`は明示したstagingディレクトリにだけ配置します。稼働領域へのコピー、アカウント作成、DB初期化、SMF登録・有効化は実行しません。同じstaging先へ再実行するとプログラムと`.sample`を更新し、既存の設定本文は保持します。

```sh
python3.11 scripts/install-solaris.py --destdir "$PWD/build/solaris-stage" --python /usr/bin/python3.11
/usr/sbin/svccfg validate build/solaris-stage/opt/liner-notes/share/smf/liner-notes.xml
```

Pythonの絶対パスは実際の検証済みインタープリターに合わせます。既定の予定配置は次のとおりです。

| 内容 | 配置 |
|---|---|
| BBS本体 | `/opt/liner-notes/sbin/liner-notes` |
| Door排他helper | `/opt/liner-notes/libexec/liner-notes-door-lock`（非特権、0755） |
| 管理ツール | `/opt/liner-notes/bin/liner-notes-admin`など |
| HELP・メニュー・バナー | `/opt/liner-notes/share/liner-notes/` |
| 設定 | `/etc/liner-notes/` |
| 会員・記事・メール・保管庫 | `/var/opt/liner-notes/` |
| アプリケーションログ | `/var/log/liner-notes/` |
| SMF登録用マニフェスト | `/opt/liner-notes/share/smf/liner-notes.xml` |
| SMFサービス | `svc:/application/liner-notes:default` |

SMFは専用ユーザー・グループ`liner_notes`、`basic`権限、初期状態無効で定義します。ポートはlocalhostの2222です。23番ポートの特権やrootでのBBS起動は既定で与えません。

起動設定`solaris.json`は`program`、`working_directory`、`arguments`の3項目です。`arguments`は文字列の配列で、シェル展開・evalはしません。管理者所有、サービスアカウントから読み取り可能、グループ・その他から書き込み不可としてください。起動前の読み取り専用検査は`liner-notes-smf check /etc/liner-notes/solaris.json`で実行できます。

SMFのcontractモデルで子プロセスを追跡し、停止は`:kill`で当該サービスのcontract内に限定します。起動直後の異常終了を開始失敗として返し、起動後の契約終了・障害処理はsvc.startdへ委ねます。標準出力・エラーはSMFのサービスログへ出します。PID名を使った全システム一括終了はしません。

`svcs`のonline状態だけでBBSへのログイン成功を判断せず、待受と実接続も確認します。SMF登録・自動起動・異常終了後の状態遷移は実機試験待ちです。`svccfg validate`成功は登録・稼働成功とは別です。

Doorを有効にするときは同じビルドの排他helperを本体と一緒に配置してください。ビルド領域では本体と同じディレクトリ、導入後は本体から見た`../libexec/liner-notes-door-lock`を使用します。PATHやデータディレクトリから探しません。所有者がrootまたは実行ユーザーでない、他者が書き込める、setuid/setgid付き、実行できないhelperは拒否します。実体の親ディレクトリも所有者と書込権を検査します（root所有のstickyな一時ディレクトリだけは例外）。`liner-notes-doctor --check-doors`でも、SolarisでDoorを設定している場合に配置・権限を読み取り専用で診断します。欠落・不正時はDoor設定を持つ本体の起動を拒否します。

## 検証済みの機能

- Solaris 11.4 CBE x86-64、GCC15、Python3.13.9で本体・管理コマンドのビルドと基本BBS回帰全体、SQLiteの別コピー変換・バックアップ・復元、doctor 33件と原本非変更、C/Python排他寿命、SMF staging・定義検証が成功しました。DoorはNAWS・複数接続/枠・異常終了/切断/子孫回収、WebUIはHTTP・CSRF・私信・転送・実PTY、BBS間リンクは3局スター/リング・mTLS・再送・再起動・チェックポイントまで成功しています。
- lrzsz0.13.1による実送受信、バイナリ・空ファイル、SHA-256一致、日英メニュー、メール添付、一括転送、通信切断後の途中再開、同じ接続からの再試行、保存済み登録先を使うZRESUMEが成功しました。転送子プロセス24並列と制限超過拒否・停止回収、タイムアウト後の端末属性の完全復元、転送中の会員権限取り消しも検証しています。これは自動試験の範囲であり、Solarisでの本番規模の同時大容量転送性能を保証するものではありません。
- WebUI・リンクの検証では、専用venvへaiohttp3.14.5を導入し、既存のcryptography46.0.3と組み合わせて成功しました。system PythonとOSパッケージは変更していません。
## 現在の制約

- CIアカウントにはSMF管理権限がありません。既存OS設定や権限を自動変更せず、実サービス登録・稼働は未検証とします。
- SMFマニフェストと設定保持を行うstaging installerを用意しています。実SMFでの登録・起動・停止・自動起動、および本番用の導入・更新手順は未検証です。FreeBSDのrc.dやLinuxのsystemdの手順をそのまま使用しないでください。
- COM・モデムコネクタはFreeBSD/Linux向けです。Solaris対応はありません。
- FreeBSD pkg、RPM、DEBをSolarisへ導入できるという意味ではありません。

設定やDBの意味は共通です。[保存方式](storage.md)、[バックアップ](backup-recovery.md)、[Door設定](external-games.md)も参照してください。OS固有のサービス操作は実機確認が完了するまで正式手順として公開しません。
