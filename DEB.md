# Debian 13（trixie）向けDEBパッケージ（開発中）

基準環境はDebian 13 amd64です。Ubuntu検証機を再構築せず、CIの隔離されたDebian環境で生成・検証します。

## パッケージ

- `liner-notes`：BBS本体、管理ツール、systemd unit。
- `liner-notes-web`：WebUI（`python3-aiohttp >= 3.11`）。
- `liner-notes-link`：BBS間リンク（`python3-cryptography >= 43`）。

Debian 12の標準ライブラリは両方の依存バージョンを満たしません。**Debian 12やUbuntuでの動作を保証しません。** 本番の`debian.ysni.org`はリリース前にOSバージョンと既存パッケージ構成を読み取り専用で確認してください。

## ビルド

使い捨てDebian 13環境で以下を実行します。

```sh
apt-get update
apt-get install -y build-essential debhelper devscripts clang libssl-dev libsqlite3-dev python3
dpkg-buildpackage -b -us -uc
```

CIの`Debian 13 Package CI`では、3つのDEBを生成し、隔離環境へのインストール・削除・データ保持を検証します。`SHA256SUMS`と`BUILD-INFO.txt`に検証元コミットを記録します。**GitHub Releasesへの公開と本番更新は行いません。**

- パッケージ導入・更新では、サービスの起動・停止・自動有効化・再起動を禁止します。
- 既存の`/etc/liner-notes`はDebian conffileとして保持し、データ・鍵はパッケージへ含めません。
- Ubuntuへの同一DEBの流用は、OSごとの依存・ABIの検証が完了するまではしません。
- ロールバック方法とpkg/RPM/DEBのCI検証が完成するまで、外部3局へのCDは無効です。
