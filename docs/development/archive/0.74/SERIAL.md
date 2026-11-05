# COM・モデムコネクタ

`scripts/serial_connector.py` はPOSIXのシリアル端末を、同一ホストの
Liner notes Telnet待受へ接続します。認証、掲示板の権限、無入力制限は
BBS本体が担当します。専用会員や自動ログインは作りません。

Python 3.11以降、FreeBSDまたはLinuxを使用します。Windows COMは未対応です。
`--platform freebsd` / `--platform linux` でOS用の設定を選択できます。
省略時は `auto` で自動判定します。実行OSと異なる指定は起動時に拒否します。
FreeBSDではRTS/CTSの方向別フラグも扱い、LinuxではCRTSCTSを使用します。
同じデバイスにgetty等を同時に起動しないでください。デバイスは排他で開き、
終了時に端末設定を戻します。実機ではデバイスへの必要な読み書き権限だけを
実行ユーザーへ与えます。BBS本体をrootにする必要はありません。

## まず疑似端末で確認

ソースのルートで `make all` の後に実行します。

```sh
python3.12 tests/test_serial_connector.py ./runtime
```

実BBSへの接続・会員登録・パスワード非表示・切断、全256値のバイナリの
双方向転送、Telnet制御の分割受信、RING→ATA→CONNECT、端末設定の復元を
検証します。物理モデム、DCD/DTR、RTS/CTSの電気的動作を実証するテストでは
ありません。実機導入前に着信・切断・再着信と使用する転送方式を確認してください。

## 直接接続

例のデバイスと速度は実機に合わせて変更します。BBSには `--telnet --menu`
が必要です。コネクタの接続先はループバックアドレスに限定しています。

```sh
python3.12 /usr/local/share/liner-notes/scripts/serial_connector.py \
  --platform freebsd \
  --device /dev/cuaU0 --baud 115200 --flow rtscts \
  --host 127.0.0.1 --port 23 --mode direct
```

DCD/DTRがない直接ケーブルや疑似端末の試験だけは `--ignore-carrier` を
指定できます。この場合、物理切断はキャリアで検知できません。既定では
300秒の無通信でも接続を終了します。BBS本体の管理者の無入力免除とは独立した
回線保護です。`--timeout 0` でこのコネクタ側の制限を無効にできます。

## Hayes互換モデム

```sh
python3.12 /usr/local/share/liner-notes/scripts/serial_connector.py \
  --platform freebsd \
  --device /dev/cuaU0 --baud 115200 --flow rtscts \
  --host 127.0.0.1 --port 23 --mode modem
```

初期化は `ATE0Q0V1&C1&D2S0=0`、RINGでATAを送ります。機種が対応する
初期化命令を `--init 'AT...'` で指定できます。接続中は本文から
`NO CARRIER` を探さず、DCDで切断を検知します。BBS切断時はDTRを落とし、
新たな着信を待ちます。モデムで `--ignore-carrier` を使うのは疑似端末試験に
限ってください。キャリアのない状態でセッションを引き継がせないためです。

プロセス監視にはFreeBSDのdaemonやLinuxのsystemdを使用できます。
開始時の接続失敗・装置エラーは非ゼロで終了するため、監視側に再起動間隔を
設定してください。ログには接続・切断理由だけを出し、通信内容は保存しません。

## Linuxで選択する例

Rocky Linux / AlmaLinuxではPython 3.11以降の実行ファイル名を確認してください。
USBシリアルの例は `/dev/ttyUSB0`、固定ポートの例は `/dev/ttyS0` です。
アクセス権はudevや専用グループで設定し、同じポートのserial-gettyを併用しません。

```sh
python3.12 /usr/local/share/liner-notes/scripts/serial_connector.py \
  --platform linux --device /dev/ttyUSB0 --baud 115200 --flow rtscts \
  --host 127.0.0.1 --port 23 --mode modem
```

疑似端末の自動テストは両OSで同じコマンドを使用します。
OSの指定を受け付けることと、物理モデムで検証済みであることは別です。

Solaris / illumosは将来の候補として残します。現時点の`auto`は未対応OSを
拒否します。実機または適切な検証環境を確保してから、速度定数・フロー制御・
キャリア検出・PTY・サービス起動を追加検証します。

参考: [FreeBSD termios(4)](https://man.freebsd.org/cgi/man.cgi?query=termios&sektion=4)、
[Python termios](https://docs.python.org/3/library/termios.html)。
