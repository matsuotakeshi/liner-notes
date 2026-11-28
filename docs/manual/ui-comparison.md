# 端末UI別の主な入口

8種類の端末UIは同じ会員・ボードと返信関係を共有します。WebUIは別のブラウザー画面です。下表は各UIの主画面または明記したサブメニューの入口です。各画面でのキーは同じ意味とは限りません。局の設定・会員権限で利用できない場合があります。

| UI | ボード | メール | 保管庫 | 一行会議 | TOPIC | ゲーム | 詳細 |
|---|---|---|---|---|---|---|---|
| RTBBS | `B` → ボード → `L` | `M` | `F` | `LINECONF` | `TOPIC` | `GAMES` | [RTBBS](ui-rtbbs.md) |
| MMM | `2` / `open` | `1` / `mail` | `3` | `talk` | `TOPIC` | `6` / `games` | [MMM](ui-mmm.md) |
| MASH | `2` / `open` | `1` / `mail` | `5` | `14` / `talk` | `TOPIC` | `6` → `4` | [MASH](ui-mash.md) |
| KTBBS | `R` → ボード → `L` | `M` | `F` | `!` → `talk` | `!` → `TOPIC` | `!` → `games` | [KTBBS](ui-ktbbs.md) |
| BIG-Model | `B` → SIG → `R` | `M` | `P` | `+` → `talk` | `TOPIC` | `+` → `games` | [BIG-Model](ui-big-model.md) |
| WANI-BBS | `b` | `mail` | `p` | `talk` | `topic` | `g` | [WANI-BBS](ui-wani-bbs.md) |
| NET-COCK | `BREAD` → `A` / `L` | `MREAD` | `PDS` | `EXCOM` → `TALK` | `EXCOM` → `TOPIC` | `GAMES` | [NET-COCK](ui-net-cock.md) |
| VS | `B` → `L` / `R` | `M` | `F` | `TALK` | `TOPIC` | `GAMES` | [VS](ui-vs.md) |

主画面の `UI` は局が切替を許可した場合の共通入口です。ログイン後のMMM系UIの `LN` は、共通拡張の一覧を表示します。入力・転送・ゲーム中は終了してから切り替えます。Webの操作は[WebUI](web-users.md)をご覧ください。

フラット画面は返信を記事番号順に表示します。MMM・MASHはHyperNotes型、WANIは通常またはマルチノーツ、VSは番号順または関連連続の巡回を使用します。保存された返信関係は共通で、見え方と巡回操作はUIごとに異なります。VSのジャンプレスとVS固有の一行ボードはv0.76では提供されません。

<div lang="en" markdown="1">

## English quick guide

The terminal profiles are `rtbbs`, `mmm`, `mash`, `ktbbs`, `big-model`, `wani-bbs`, `net-cock` and `vs`. The browser WebUI connects to the same BBS through its separately configured service. Member records and article reply relationships are shared. RTBBS, KTBBS, BIG-Model and NET-COCK list replies flat. MMM and MASH show a reply tree. WANI-BBS also has multi-notes, and VS can follow related articles during news reading.

From the terminal's main menu, enter `UI` and select an allowed number or enter `UI mmm` (substitute any other allowed identifier). `UI RESET` clears a saved choice and restores the site's default. `UI CANCEL` exits the selection. Finish edits and transfers or leave a Door before switching. `FIXED` disables switching, `MEMBERS` limits it to signed-in members, and `ALL` also permits a temporary guest choice. Stored choices remain subject to the site's allowlist.

- [OS installation](install-freebsd.md) · [Linux](install-linux.md) · [Solaris](install-solaris.md)
- [FreeBSD and Linux settings](configuration-os-map.md) · [WebUI](web-users.md)
- [Sharing boards](links.md) · [Link status, maintenance and recovery](link-operations.md)

On FreeBSD, set the packaged service's overrides in `/etc/rc.conf` and use `service liner_notes restart`. On Rocky and Debian, insert options before the positional port in `LINER_NOTES_OPTIONS` in `/etc/liner-notes/liner-notes.env` and use `systemctl restart liner-notes`. Changing the unit file itself requires `systemctl daemon-reload`. The box above and the linked Japanese pages provide the interface-specific command references and configuration details.

</div>
