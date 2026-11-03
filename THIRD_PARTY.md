# Licensing and third-party components

Copyright (C) 2026 Takeshi Matsuo and Liner notes contributors.
Liner notes original source and documentation are distributed under the GNU
General Public License version 3 only (`GPL-3.0-only`). See [LICENSE](LICENSE).
The software is provided without warranty. This release does not relicense
third-party materials or change the terms of previously published releases.

Bundled browser dependencies retain their MIT licenses:

| Files | License notice |
|---|---|
| `webui/static/xterm.js`, `xterm.css` | `webui/static/xterm-LICENSE` |
| `webui/static/xterm-fit.js` | `webui/static/xterm-fit-LICENSE` |

The bundled versions are @xterm/xterm 6.0.0 and @xterm/addon-fit 0.11.0.
Their JavaScript, CSS and license notices were compared byte-for-byte (with LF
line endings) against official npm archives. The matching upstream archives,
including preferred TypeScript source, are included under `third_party/`.
See `third_party/SOURCES.json` for exact source URLs and SHA-256 hashes.

Upstream: https://github.com/xtermjs/xterm.js . These notices are installed
with the Web assets. Original copyright notices must be retained.

External dependencies (not copied into the source release): OpenSSL/libcrypto,
SQLite, Python, aiohttp, and optional lrzsz. Their own licenses apply. FreeBSD
packages record the dependencies; install lrzsz using the official port/package.
`scripts/build-lrzsz.py` is an optional isolated development tool which fetches
and patches an external upstream archive; the fetched engine is not bundled.

The Clans and StarRoute Traders are separately installed games and are not
included in this distribution. RTBBS/DOS source and original data kits are not
included. The Door interface exchanges user identity through a private drop
file or environment variables; game code is not incorporated into this BBS.

Binary redistributors must also provide the corresponding Liner notes source
under GPLv3. The versioned source archive alongside the FreeBSD package contains
the source and build scripts; the ports archive contains its packaging recipe.
