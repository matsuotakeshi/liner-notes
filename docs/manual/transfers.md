# 転送方式・途中再開・タイムアウト

## 方式と上限

`PROTOCOL ZMODEM|XMODEM-SUM|XMODEM|XMODEM-1K|YMODEM|YMODEM-G|MLINK|TEXT` で選択します。XMODEMはCRC受信、XMODEM-SUMはSUM受信、1Kは1024バイトブロックです。送信時のSUM/CRCは相手の要求によります。全ての歴史的な端末との相互運用を保証するものではありません。

UPLOAD/DOWNLOADとMAILUPLOAD/MAILDOWNLOADは選択方式、Zで始まる命令はZMODEM固定です。XMODEM・TEXTは単体だけで受信に名前と正確なサイズを指定します。YMODEM・YMODEM-g・M-LINK・ZMODEMの一括は最大16ファイル・16GiB、単体最大1GiBです。保管庫・メール添付・方式で共通の設定上限を適用します。

ZMODEMは別導入のlrzszを呼び出します。FreeBSDでは本家portsを使い、独自lrzsz portsを配布物へ同梱しません。BBSサービスアカウントで実際のlrz/lszを実行でき、spoolへ書き込める必要があります。

## 完了を確認する

端末側の送受信完了と、BBS側の検証・登録完了は別です。受信後の書き込み進捗100%だけで切断せず、最後の成功表示を待ちます。失敗時は成功とみなさず、完了状態を照合してから再試行します。一括は個別完了を記録し、成功した分だけ取得数を加算します。

WebではHTTP受信後の登録を画面から照会します。ダウンロードはストリーミングし、全体をJavaScriptメモリへ展開しません。Web端末内ZMODEMは未対応です。Webの再開はHTTP側で元ファイルを再選択し、BBS向けには確定位置から継続します。

## ZMODEM途中再開

単体は `ZRESUME`、別ボードは `ZRESUME パス`、説明を検査する場合は `ZRESUME パス|元の説明`。ルートは `ZRESUME -` です。引数なしは現在保管庫と保存済み説明を使用します。一括は `ZUPLOADBATCHRESUME`。元と同じ名前・内容・ファイル集合を送ってください。登録先の権限は再検査します。

新しい `ZUPLOAD` を選ぶと、受信途中データをretainedへ退避して新規受信します。旧受信を続けたい場合は先にZRESUMEを使います。受信済みで公開結果不明の場合は自動退避せず、照合して二重登録を防ぎます。使用中のspoolを移動しません。

サービス所有者による再開検証例:

```sh
su -m liner_notes -c '/usr/local/bin/liner-notes-check-resume /usr/local/bin/lrz /usr/local/bin/lsz --receipt /var/db/liner-notes/lrz-resume.json'
```

検証記録は既存でも再実行できます。成功時だけ作成・更新し、同じ内容なら保持、変更時は旧記録を退避します。`created` / `unchanged` / `updated` を確認してください。lrzsz交換後は再検査します。記録を手書きで作って検査を回避しません。

## タイムアウト

外部転送helperの `--timeout` は方式・送受信共通の無通信時間（既定120秒、1〜3600秒）です。データが進めば総時間が長くても継続します。外部プログラムの再送制限・CPU時間制限、本体側の独立した停止監視は別です。

Webの `[bbs] transfer_timeout` は最終照合待ち（既定120秒、30〜600秒）、ApacheのtimeoutやHTTP受信制限も別にあります。全層を同じ意味の時間制限と考えず、[Web設定](web-server.md)と[転送障害](troubleshooting.md)で切り分けます。

## 専用クライアント

line-v1・binary-v1とWeb向けCONNECTORはBBS権限・サイズ・SHA-256・完了照合を共有します。一般利用者は専用画面または端末転送を使います。独自クライアント実装は[通信仕様](protocol.md)を参照してください。

## 専用helperの生成

既定と異なるspool・lrz/lszを使う場合のFreeBSD例です。先に変更先のエンジンで再開検査を行い、その検証記録を指定します。Pythonの実行名は導入済みの版に合わせます。標準構成では既定の `liner-notes-zmodem` をそのまま使用できます。

```sh
su -m liner_notes -c '/usr/local/bin/python3.12 /usr/local/share/liner-notes/scripts/configure-zmodem.py --receiver /usr/local/bin/lrz --sender /usr/local/bin/lsz --spool-root /var/db/liner-notes/spool --resume-receipt /var/db/liner-notes/lrz-resume.json --output /var/db/liner-notes/zmodem-helper --timeout 120'
sysrc liner_notes_zmodem_helper=/var/db/liner-notes/zmodem-helper
```

spoolは実行者所有の0700ディレクトリ、helperも0700で作成します。既存の出力helperは上書きしないため、変更時は別名へ生成し、内容と所有者を確認してから起動設定を切り替えます。生成物には実行したPythonとスクリプトの絶対パスが入ります。Python更新時にも確認してください。BBSの再起動は転送中の利用者がいない時間に行います。
