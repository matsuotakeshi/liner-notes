## GitHub-hosted Ubuntuの配布ミラー障害

2026-11-19、設定診断CIの準備でUbuntu既定ミラーの503・接続失敗が再発しました。
`doctor.yml`と`shared-visibility.yml`は、`ci/ubuntu_apt_mirror.py`で
`https://mirrors.edge.kernel.org/ubuntu/`へ取得先を切り替えます。
対象は使い捨てのGitHub-hosted Ubuntu 24.04 amd64だけです。
既存のUbuntu URIだけを置換し、Suites、Components、Signed-By、署名・有効期限の
検証は維持します。不明な取得先は変更前に拒否し、元の設定を同じRunner内に保存します。
他社リポジトリ、自宅Runner、検証VM、本番のAPT設定は変更しません。

`apt-get update --error-on=any`でメタデータ取得の失敗も失敗扱いにし、古い一覧を
代用して先へ進みません。Playwrightの依存パッケージも同じRunnerの取得先を使います。
テスト内容、必須チェック、main限定条件は変更しません。
代替ミラーも失敗した場合は準備失敗として停止し、無限再試行や検証省略はしません。
この切替の回帰テストは両ワークフローの設定変更前に実行します。
