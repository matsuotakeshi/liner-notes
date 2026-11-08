> この文書は機能追加時点の記録です。現行の導入入口は [README](README.md)、今回の変更は [STAGE65](STAGE65.md) を参照してください。古い段階名・上限・未実装事項は当時のものです。

# ZMODEMコネクタの試作（stage38）

現在のメニュー統合・導入方法は **[stage49の説明](ZMODEM_MENU.md)** を参照してください。以下は各段階で追加した機能と検証の記録です。

SSH端末のZMODEM送受信と、Liner notesの会員向け保管庫を接続する独立した入口です。既存のC製BBSには手を加えず、会員認証と公開・読み出しは既存の送受信APIを利用します。ZMODEM処理はC製のlrzszを別プロセスで動かします。

今回の入口は `scripts/vault_zmodem.py` です。runtimeのTelnetメニューにZMODEM項目を追加する段階は含みません。標準入出力は8ビット透過のSSH接続向けです。Telnetソケットへ直接つなぐとIAC処理が必要になるため、その使い方は未対応です。既存の `--connector line-v1` / `binary-v1` は従来どおり使用できます。

## 導入

FreeBSDにPython 3、Cコンパイラー、makeを用意します。専用ディレクトリに一般ユーザー権限でビルドでき、システムへのインストールは不要です。

```sh
cd /home/operator/liner-notes-stage38-20261026
python3 scripts/build-lrzsz.py deps/zmodem
```

`deps/zmodem` は未作成のパスを指定します。公式アーカイブを取得してSHA-256を照合し、ソース・ライセンス・ビルドログを同じディレクトリへ保存します。取得済みアーカイブには `--archive /path/to/lrzsz-0.13.1.tar.gz` を指定できます。

試作は **lrzsz 0.13.1** に固定しています。旧版や未知の版で起動せず、送信と受信の実行ファイルを確認します。FreeBSDでは上流ビルド設定が `S_IFMT` を隠すため、CFLAGSで `_POSIX_C_SOURCE` を解除して標準の公開範囲でビルドします。stage56では、64ビット環境の再開CRC比較を修正する1行のパッチを別のソース展開先へ適用します。原本アーカイブ・パッチ・ビルドログと受信プログラムの検証用ハッシュを保存します。stage55以前のエンジンは通常転送には使えますが、新しい途中再開には修正版が必要です。

[公式配布ページ](https://www.ohse.de/uwe/software/lrzsz.html) / [公式ソースアーカイブ](https://www.ohse.de/uwe/releases/lrzsz-0.13.1.tar.gz)

```text
SHA-256: 112f91059fbd118e016d800c1a5c5a1702e965d8b9bfbda6015e44e20bfb93c9
```

lrzszは別途取得するGPLの外部プログラムです。この配布アーカイブには上流コードや実行ファイルを含めません。ライセンスは取得したソースのCOPYINGを参照してください。独立プロセスとして呼び出し、BBSへのリンクやソース取り込みは行いません。

## 使用方法

保管庫を有効にしたruntimeを起動しておきます。例:

```sh
./runtime --telnet --menu --vault-dir /home/operator/notes-vault 2222 notes.dat members.dat
```

ZMODEM対応の端末ソフトから、SSHのリモートコマンドとして専用入口を起動します。SSHのOSアカウントと、`--user` のBBS会員名は別です。BBSパスワードは対話入力します。

受信（端末のファイルをBBSへアップロード）:

```sh
ssh -t operator@192.0.2.10 'cd /home/operator/liner-notes-stage38-20261026 && python3 scripts/vault_zmodem.py receive --user bob --receiver ./deps/zmodem/local/bin/lrz --sender ./deps/zmodem/local/bin/lsz'
```

BBSパスワードを入力後、端末側の「ZMODEM送信」でファイルを1つ選びます。ZMODEMの受信完了に続いて保管庫へ登録します。**「ファイル #番号 を公開しました」までが成功確認**です。端末側の転送完了表示だけでは、BBSへの公開はまだ確認できていません。

送信（BBSのファイルを端末へダウンロード）:

```sh
ssh -t operator@192.0.2.10 'cd /home/operator/liner-notes-stage38-20261026 && python3 scripts/vault_zmodem.py send --user bob --id 1 --name sample.zip --receiver ./deps/zmodem/local/bin/lrz --sender ./deps/zmodem/local/bin/lsz'
```

端末側でZMODEM受信を開始します。`--name` 省略時は `file-1.bin` のような名前です。ダウンロード先の既存ファイルは上書きしません。相手側がファイルをスキップした場合も成功扱いにしません。

- `--lang ja|en`: 起動・完了・失敗案内の言語。初期値ja。
- `--description 説明`: アップロードの説明。
- `--host` / `--port`: 内部接続先のBBS。初期値127.0.0.1:2222。
- `--timeout 秒`: ZMODEM実行の上限。初期120秒、指定可能1〜3600秒。
- `--spool-root パス`: 既存の一時保存用ディレクトリ。省略時はOSの一時領域。

実機の試作配置では、検証済みエンジンは `deps/verified/local/bin/lrz` と `deps/verified/local/bin/lsz` にあります。上のコマンドの `deps/zmodem` を `deps/verified` に替えて利用できます。

GUI端末ソフトごとの自動起動や受信ダイアログの操作は未検証です。実通信試験ではlrzszを相手として使用しています。SSHの専用コマンドとして起動し、処理が終わったら接続も終了する運用を想定しています。

## 保存と失敗時の扱い

1回1ファイル、上限16MiBです。受信は専用の0700ディレクトリへ行い、パス・ディレクトリ・隠しファイル・リンク・制御文字を含む名前を受け付けません。実行ファイルへの引数に受信した名前をシェル展開することはありません。受信エンジンは制限モードで動き、BBSパスワード、内部接続ソケット、会員ファイル、保管庫ディレクトリを受け取りません。

ZMODEMセッションが正常終了し、ちょうど1つの通常ファイルを受け取ったことを確認してからSHA-256を計算し、既存の予約・分割保存・確定APIで公開します。ダウンロードも全体のSHA-256を確認してから端末へ送ります。ZMODEMのCRC検査と、保管庫のSHA-256検査は別々に行います。

受信途中の失敗・キャンセル・複数ファイル・時間超過では公開せず、一時ファイルを削除します。ZMODEM中断位置からの再開は今回の対象外です。もう一度、最初から送信してください。

公開処理が始まった後に応答を失った場合は、元ファイルと `upload.json` の保存先を表示し、削除しません。次の既存クライアントで公開結果の確認・再開ができます。同じ接続先・会員を指定します。

```sh
python3 scripts/vault-upload.py --host 127.0.0.1 --port 2222 --user bob --state /表示された保存先/upload.json --resume /表示された保存先/data/元のファイル名
```

開始応答を失って予約IDが分からない場合、既存の手順どおり管理者のFILEAUDITで確認します。自動で重複予約しません。必要なくなった回復用ディレクトリは、公開結果を確認した後に利用者が片付けてください。

## 分離と制限

`ZmodemProcess` は通信FD・専用ディレクトリ・認可確認コールバックだけを受け取ります。`vault_zmodem.py` が会員接続と保存手順を担当します。ZMODEMエンジンを交換しても保管庫や会員管理を直接参照する必要はありません。

転送中は約1秒ごとに会員状態を再確認し、ダウンロードでは公開状態も確認します。停止・切断・非公開化を検出したら子プロセスを終了します。すでに端末へ送ったデータを取り戻す機能ではありません。再確認と送信には時間差があり、ただちに1バイト単位で遮断する保証はありません。公開前には既存API側でも認可されます。

ファイル単体はOSの書き込み上限で制限します。複数ファイルや一時領域の異常は監視して中止しますが、OSのディスククォータや専用jailの代わりではありません。これは信頼できるOSアカウントがSSHで実行する試作入口です。公開Telnetサービスへの接続、複数利用者向けの同時転送制限、一時領域の総量制限、メニューへの統合は次段階です。

## 検証方法

```sh
python3 tests/test_zmodem_workflows.py
make test-zmodem LRZ=./deps/zmodem/local/bin/lrz LSZ=./deps/zmodem/local/bin/lsz
```

実エンジン試験は追加依存が必要なので通常の `make test` と分けています。依存なしの保存手順試験は `make test` に含みます。実機試験は専用データと一時ポートを使います。

FreeBSD 15.1実機で上記2試験と既存の転送手順・ネットワーク送受信・binary-v1試験が成功しました。全バイト・空ファイル、実際のCLI入口、既存ファイルのスキップ、バッチ・キャンセル・容量超過・親ディレクトリを含む名前・時間超過・会員停止、PTY設定の復元を確認しました。Windowsではエンジンに依存しない2試験が成功しています。記録は `evidence/freebsd-stage38.txt` と `evidence/windows-stage38.txt`。
