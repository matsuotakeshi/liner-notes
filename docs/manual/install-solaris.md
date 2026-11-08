# Solarisへの導入

## 次期版（未リリース）：移植検証中

対象はOracle Solaris 11.4 CBE x86-64です。安定版0.74.1の正式対応OSには含まれません。SPARC・ARMは対象外です。現在はネイティブビルド・機能試験の基盤を用意した段階で、実機試験の成功や全機能対応を意味しません。

## 検証と依存関係

`Solaris Native CI`は信頼済みmainの正確なコミットだけを、既存のCIアカウントと固定ホスト鍵で取得します。独立した作業ディレクトリでビルドし、OSのパッケージ導入やサービス変更を行いません。PRでは接続を伴わないCI制御のテストを実行します。従来のFreeBSD・Rocky Linux・Ubuntuの3環境の検証は維持しています。

必要な構築環境はGNU make、64ビットC11コンパイラ（ClangまたはGCC）、SQLiteとOpenSSLの開発用ヘッダー・ライブラリ、Python 3.11以上です。読み取り専用診断ツールdoctorも含めて検証します。WebUIとBBS間リンクは、それぞれaiohttp、cryptographyを必要とします。ZMODEM相互運用試験はlrz/lszを使います。未導入の場合、既存の公式ソース取得・SHA-256検証helperで作業コピー内だけにビルドします。Solarisではgcc/gmakeと64ビット指定を使用し、OSのパッケージは変更しません。

CIログにはOS、コンパイラ候補、ライブラリ配置、Python、正確なソースSHAを記録します。OSパッケージは自動導入しません。WebUI・リンクのPython依存は作業コピー内の専用venvにrequirementsから導入し、既存のsystem Pythonを変更しません。インストール失敗もCI失敗として記録します。基本BBS、Door、WebUI、リンク、ZMODEMの検証状態は`SOLARIS_CAPABILITIES`行で区別します。追加依存がなく実行できなかった機能を成功扱いしません。

## ビルド上のOS差

Solaris 11.4の`openpty`と`login_tty`はlibcにあり、`termios.h`で宣言されます。FreeBSD/Linux向けの`libutil`をSolarisへリンクしません。ソケットは`-lsocket -lnsl`を明示します。既存のPTYと端末サイズ伝達を維持し、端末の制御取得はSolarisの`login_tty`へ委ねます。SolarisにはBSDの`flock`がないため、専用の小さな子プロセスがPOSIXファイルロックを保持します。BBSとDoorへ継承したpipeがすべて閉じるまで保持し、BBSの異常終了後も生存中のDoorが同じ枠を占有します。FreeBSD/Linuxは従来の`flock`を使用します。

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

## 現在の制約

- 最初の実機CIではGCC15の厳密な警告とBSD flockの未提供を確認しました。警告の抑制はせず、該当箇所の改行と排他方式を修正中です。ビルド・機能試験の成功はまだ確認できていません。
- 検証機にはPython3.11/3.13、cryptography、OpenSSL3、SQLite3.51がありますが、aiohttpはありません。CIでは作業コピー内の専用venvへrequirementsからaiohttpを導入してからWebUIを検証します。依存導入・機能試験の成否は次の実機CIで確認します。
- CIアカウントにはSMF管理権限がありません。既存OS設定や権限を自動変更せず、実サービス登録・稼働は未検証とします。
- SMFマニフェストと設定保持を行うstaging installerを用意しています。実SMFでの登録・起動・停止・自動起動、および本番用の導入・更新手順は未検証です。FreeBSDのrc.dやLinuxのsystemdの手順をそのまま使用しないでください。
- COM・モデムコネクタはFreeBSD/Linux向けです。Solaris対応はありません。
- FreeBSD pkg、RPM、DEBをSolarisへ導入できるという意味ではありません。

設定やDBの意味は共通です。[保存方式](storage.md)、[バックアップ](backup-recovery.md)、[Door設定](external-games.md)も参照してください。OS固有のサービス操作は実機確認が完了するまで正式手順として公開しません。
