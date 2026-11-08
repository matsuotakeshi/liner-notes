# Telnet公開・ポート23・SSH

<a id="public-telnet"></a>

## 外部公開

管理者登録、送受信、初期バックアップが終わったら外部待受へ変更します。

```sh
sysrc liner_notes_bind=0.0.0.0
service liner_notes restart
sockstat -4l -P tcp -p 2222
```

これは全IPv4アドレスでの待受です。特定のアドレスだけで公開したい場合は、そのIPv4アドレスを指定します。IPv6待受を追加する設定ではありません。

使用中のファイアウォールでTCP 2222を許可します。NATがある場合は転送先をBBSサーバーへ合わせ、DNSのAレコードが公開先を指していることを確認します。既存のファイアウォール設定全体を置き換える必要はありません。

**別の端末**から次を実行します。

```sh
telnet bbs.example.org 2222
```

Telnetは暗号化されません。BBS用のパスワードを他サービスと共用しないでください。暗号化された入口にはOSのOpenSSHを使えます。すでにSSHでログインできるOSアカウントなら、クライアントから次の形で接続します。

```sh
ssh -t OSアカウント@bbs.example.org /usr/local/bin/liner-notes-ssh 2222
```

この入口はBBS内蔵SSHではなく、sshdやOSアカウントを自動設定しません。OSの `liner_notes` はサービス専用なので、利用者のSSHログイン用には使いません。SSH経由のファイル転送は、使用する端末でも別途確認してください。

### 標準のTelnetポート23で待ち受ける

2222で起動できても、通常のサービスユーザーには1024未満のポートをbindする権限がありません。`liner_notes_run_user=root`へ変えるとDoorゲームも使えなくなります。FreeBSDでは `mac_portacl` でサービスのUIDにTCP 23だけを許可できます。**すでに23で動いている環境では、このOS設定を変更する必要はありません。**

まず現在の待受と設定を確認します。

```sh
sockstat -4l -P tcp -p 23
id liner_notes
sysctl net.inet.ip.portrange.reservedlow net.inet.ip.portrange.reservedhigh
kldstat -m mac_portacl || kldload mac_portacl
sysctl security.mac.portacl
```

以下は通常の新規ホストで、既定の低番号ポート保護をMACポリシーへ置き換える例です。既存のMAC・jail・他サービスのポート制御があるホストでは、その運用規則と統合してください。`reservedhigh=0`だけを先に設定せず、MACの保護と許可規則を有効にしてから変更します。許可は実行ファイル名ではなくUIDに結び付きます。[FreeBSD mac_portacl(4)](https://man.freebsd.org/cgi/man.cgi?query=mac_portacl&sektion=4)の仕様に基づきます。

```sh
(
  set -eu
  LN_UID=$(id -u liner_notes)
  LN_RULE="uid:${LN_UID}:tcp:23"
  LN_RULES=$(sysctl -n security.mac.portacl.rules)
  case ",$LN_RULES," in
    *",$LN_RULE,"*) ;;
    *) LN_RULES="${LN_RULES:+$LN_RULES,}$LN_RULE" ;;
  esac
  sysctl security.mac.portacl.rules="$LN_RULES"
  LN_HIGH=$(sysctl -n security.mac.portacl.port_high)
  if [ "$LN_HIGH" -lt 1023 ]; then sysctl security.mac.portacl.port_high=1023; fi
  sysctl security.mac.portacl.suser_exempt=1
  sysctl security.mac.portacl.enabled=1
  sysctl net.inet.ip.portrange.reservedhigh=0
)
```

次に再起動後も有効になるよう、既存設定を保存してから永続化します。

```sh
LN_OS_BACKUP=$(mktemp -d /root/liner-notes-port23.XXXXXXXX)
for LN_FILE in /boot/loader.conf /etc/sysctl.conf; do
  if [ -f "$LN_FILE" ]; then cp -p "$LN_FILE" "$LN_OS_BACKUP/$(basename "$LN_FILE")"; fi
done
sysrc -f /boot/loader.conf mac_portacl_load=YES
sysctl security.mac.portacl.rules security.mac.portacl.port_high
ee /etc/sysctl.conf
```

`/etc/sysctl.conf`の同名項目を編集します。重複して追記せず、**rulesには上で確認した全既存規則を含めます**。64964は既定UIDの例です。既存の `port_high` が1023より大きければ、その値を維持します。

```text
security.mac.portacl.port_high=1023
security.mac.portacl.suser_exempt=1
security.mac.portacl.rules=uid:64964:tcp:23
security.mac.portacl.enabled=1
net.inet.ip.portrange.reservedhigh=0
```

ここでは実行中の値はすでに変更済みなので、BBSのポートを変更して確認します。

```sh
sysrc liner_notes_port=23
service liner_notes restart
service liner_notes status
sockstat -4l -P tcp -p 23
telnet 127.0.0.1 23
```

正常ならruntimeが23で待ち受け、メニューが表示されます。外部公開する場合はTCP 23のファイアウォール・NATも揃えます。WebUIを導入済みなら `web.toml` の `[bbs] port=23` も変更し、Webサービスを再起動します。保守時間にOS再起動後の状態も確認してください。設定途中で失敗した場合は2222へ戻してBBSを再起動し、MAC設定を無条件に解除することは避けます。

## SSH経由の入口

FreeBSDの既存OpenSSHへ `ssh -t account@host 'liner-notes-ssh 2222'` のように接続します。ラッパーは `/usr/bin/telnet` を使い、同一ホストのIPv4ループバックへつなぎます。BBS専用SSHサーバーではありません。SSHアカウントの制限は設置者がOpenSSH側で管理し、BBS会員認証は別に行います。

TTYが必要です。ラッパーはシェルエスケープと.telnetrcを無効にしますが、OSアカウント自体のシェル権限を制限する代わりにはなりません。Linuxのtelnetオプション互換は保証しないため、FreeBSD向け入口として扱います。

## Telnetと端末サイズ

本体はTelnet BINARY・NAWS等を扱います。バイナリ転送は対応端末を使用し、単純なソケット試験とTelnet対応試験を混同しません。NAWSで通知した幅・高さを通常画面とDoor PTYへ反映します。サイズ範囲とDoor側の実装は[Door](external-games.md)を参照してください。直接待受はIPv4で、IPv6直接待受は提供しません。
