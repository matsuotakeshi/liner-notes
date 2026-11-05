# 第31段階: 外部TelnetとSSH経由の接続

## 外部Telnet

数値IPv4の `--bind` を追加しました。省略時は従来どおり127.0.0.1です。

```sh
./runtime --bind 0.0.0.0 --telnet --menu --menu-dir ./menus 2222 notes.dat members.dat
```

別PCから接続します（文字コードUTF-8）。

```sh
telnet 192.0.2.10 2222
```

`--bind 0.0.0.0` はすべてのIPv4インターフェースで待ち受けます。特定のインターフェースに絞る場合は `--bind 192.0.2.10` のように指定します。IPv6とホスト名指定は今回の対象外です。起動時のLISTEN行に実際の指定アドレスとポートを表示します。

Telnetはパスワードや記事内容を暗号化しません。インターネット越しの利用には下記SSH経由を推奨します。外部からの到達にはOSのファイアウォールやルーター設定も関係します。本変更はそれらを自動変更しません。

## SSHで直接BBSを開く

FreeBSD標準のOpenSSHとTelnetクライアントを利用する `scripts/liner-notes-ssh` を追加しました。SSHの認証・暗号化はOpenSSHが担当し、ログイン後に同じマシンの127.0.0.1へ接続します。保存ファイルを開くBBSプロセスは既存の1つだけなので、Telnet利用者とSSH利用者は同じ掲示板・会員・操作履歴を利用します。

サーバーが `--bind 0.0.0.0` または既定の127.0.0.1で起動している状態で、クライアントから実行します。

```sh
ssh -t operator@192.0.2.10 'sh /home/operator/liner-notes-stage31-20261026/scripts/liner-notes-ssh 2222'
```

SSHのOSアカウント認証後、BBSのメニューが表示されます。BBS側では別途会員登録・ログインします。SSHアカウントをBBS管理者として自動認識する処理はありません。BBSのQで切断し、そのSSHセッションも終了します。SSHだけで公開する場合はBBSをlocalhost待受のままにできます。

入口スクリプトはPTYが必要なので `ssh -t` を使います。引数はポートのみ（省略時2222）。接続先は127.0.0.1に固定しています。特定のLAN IPだけにbindしたサーバーにはこの入口からは接続できません。Telnetのコマンド用エスケープを無効化し、.telnetrcの読込とOSユーザー名による自動ログイン交渉も無効化しています。

この方式はruntime内部にSSHサーバーを実装したものではありません。サーバー側に `/usr/bin/telnet`、SSHログイン権限、PTY割当が必要です。

## BBS専用SSHアカウントを用意する場合

普段の管理用アカウントをそのままBBS専用に変更しないでください。専用アカウントを作成して、管理者が所有する固定パスへ入口スクリプトを設置した後、必要に応じてsshd_configのMatch Userに次のような設定を追加できます（例示・未適用）。

```text
Match User bbs
    ForceCommand /bin/sh /usr/local/libexec/liner-notes-ssh 2222
    DisableForwarding yes
    PermitTTY yes
    PermitUserRC no
    PermitTunnel no
Match all
```

その場合は `ssh -t bbs@192.0.2.10` で入口を起動できます。アカウントの認証方法・鍵・パスワード、ホームディレクトリ、配置先の所有者と権限は別途設定してください。ForceCommandだけで転送を禁止したことにはなりません。既存のSSH設定・アカウント・SSHサービスは今回変更していません。

参照: [FreeBSD telnet(1)](https://man.freebsd.org/cgi/man.cgi?manpath=FreeBSD+15.1-RELEASE&query=telnet&sektion=1)、[OpenSSH sshd_config(5)](https://man.openbsd.org/sshd_config)。FreeBSD実機のmanでも -E / -c / -K の仕様を確認しました。

## 既存データで新版を起動する

新版を別ディレクトリに置く場合は、保存ファイルと既存設定への絶対パスを使ってください。稼働中の旧版を停止してから切り替えます。記事ファイルに対応するmembers・moderation・extras等を一組でバックアップしてください。

`--admins`・ボード設定・外部メニューは従来どおり併用できます。ポートを指定する前に全オプションを記述します。

## 検証

`test_bind.py` はlocalhost互換・ワイルドカード・指定IPv4・不正アドレス・ポート競合・Telnet接続を確認します。`test_ssh_entry.py` はFreeBSDの実TelnetとPTYを使い、入口の引数検証・パスワード非表示・投稿閲覧・切断・Telnetエスケープ無効化を確認します。実LANと実SSH接続の結果はevidenceに記録します。

Windows関連6系列、FreeBSD関連8系列が成功しました。さらにWindowsからFreeBSDへの実LAN Telnet接続と、実SSH接続によるログイン・投稿・閲覧・終了、お互いの投稿の参照を確認済みです。テスト用サーバーは終了済みです。検証ログは `evidence/windows-stage31.txt`・`freebsd-stage31.txt`・`lan-ssh-stage31.txt`。インターネット越しの到達性や専用アカウントのForceCommand設定は未検証です。
