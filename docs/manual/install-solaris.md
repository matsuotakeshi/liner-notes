# Solarisへの導入

## 次期版（未リリース）：移植検証中

対象はOracle Solaris 11.4 CBE x86-64です。安定版0.74.1の正式対応OSには含まれません。SPARC・ARMは対象外です。現在はネイティブビルド・機能試験の基盤を用意した段階で、実機試験の成功や全機能対応を意味しません。

## 検証と依存関係

`Solaris Native CI`は信頼済みmainの正確なコミットだけを、既存のCIアカウントと固定ホスト鍵で取得します。独立した作業ディレクトリでビルドし、OSのパッケージ導入やサービス変更を行いません。PRでは接続を伴わないCI制御のテストを実行します。従来のFreeBSD・Rocky Linux・Ubuntuの3環境の検証は維持しています。

必要な構築環境はGNU make、64ビットC11コンパイラ（ClangまたはGCC）、SQLiteとOpenSSLの開発用ヘッダー・ライブラリ、Python 3.9以上です。WebUIとBBS間リンクはPython 3.11以上に加え、それぞれaiohttp、cryptographyを必要とします。ZMODEM相互運用試験はlrz/lszが必要です。

CIログにはOS、コンパイラ候補、ライブラリ配置、Python、正確なソースSHAを記録します。不足依存はOSへ自動導入せずに報告します。基本BBS、Door、WebUI、リンク、ZMODEMの検証状態は`SOLARIS_CAPABILITIES`行で区別します。追加依存がなく実行できなかった機能を成功扱いしません。

## ビルド上のOS差

Solaris 11.4の`openpty`と`login_tty`はlibcにあり、`termios.h`で宣言されます。FreeBSD/Linux向けの`libutil`をSolarisへリンクしません。ソケットは`-lsocket -lnsl`を明示します。既存のPTY、端末サイズ伝達、Doorの排他・切断処理は維持し、端末の制御取得だけSolarisの`login_tty`へ委ねます。

独立した作業コピーでの検証入口は次のとおりです。CIでは上記のmain検証を通った後に同じコマンドを使用します。

```sh
python3 ci/solaris_ci.py inventory
python3 ci/solaris_ci.py test
```

`test`は既存の基本BBS回帰試験、DBの別コピー変換・バックアップ・復元、Door試験を実行し、依存を満たす追加機能も試験します。失敗した試験は終了コードへ反映します。

## 現在の制約

- Solarisの実機ビルドと機能試験は確認待ちです。CI結果に応じて必要な移植差分を追加します。
- SMFの起動・停止・自動起動、Solaris向けインストール・更新手順は準備中です。FreeBSDのrc.dやLinuxのsystemdの手順をそのまま使用しないでください。
- COM・モデムコネクタはFreeBSD/Linux向けです。Solaris対応はありません。
- FreeBSD pkg、RPM、DEBをSolarisへ導入できるという意味ではありません。

設定やDBの意味は共通です。[保存方式](storage.md)、[バックアップ](backup-recovery.md)、[Door設定](external-games.md)も参照してください。OS固有のサービス操作は実機確認が完了するまで正式手順として公開しません。
