PORTNAME=	liner-notes
DISTVERSION=	0.74.1
CATEGORIES=	comms
MASTER_SITES=	https://github.com/matsuotakeshi/liner-notes/releases/download/v${DISTVERSION}/
DISTNAME=	liner-notes-${DISTVERSION}

MAINTAINER=	ports@FreeBSD.org
COMMENT=	HyperNotes-style BBS with Telnet, mail, chat, and file vaults
WWW=		https://github.com/matsuotakeshi/liner-notes
LICENSE=	GPLv3 MIT
LICENSE_COMB=	multi
LICENSE_FILE_GPLv3=	${WRKSRC}/LICENSE
LICENSE_FILE_MIT=	${WRKSRC}/webui/static/xterm-LICENSE

USES=		python:3.11+ shebangfix sqlite:3 ssl
RUN_DEPENDS=	${PYTHON_PKGNAMEPREFIX}sqlite3>0:databases/py-sqlite3@${PY_FLAVOR}
SHEBANG_FILES=	scripts/door_guess.py
USE_RC_SUBR=	liner_notes

NO_WRKSUBDIR=	yes
ALL_TARGET=	all
MAKE_ARGS=	CC="${CC}" CPPFLAGS="${CPPFLAGS} -I${OPENSSLINC}" \
		CFLAGS="${CFLAGS}" LDFLAGS="${LDFLAGS} -L${OPENSSLLIB}" \
		SQLITE_CFLAGS="-I${LOCALBASE}/include" \
		SQLITE_LIBS="-L${LOCALBASE}/lib -lsqlite3"

USERS=		liner_notes
GROUPS=		liner_notes
# Private allocation. Request an official UID/GID before submitting upstream.
UID_FILES=	${FILESDIR}/UIDs
GID_FILES=	${FILESDIR}/GIDs

SUB_FILES=	liner-notes-admin liner-notes-doctor liner-notes-maintenance \
		liner-notes-zmodem liner-notes-check-resume liner-notes-link liner-notes-serial \
		liner_notes.conf doors.conf pkg-message
SUB_LIST=	PYTHON_CMD=${PYTHON_CMD}

OPTIONS_DEFINE=	DOCS WEBUI ZMODEM LINK
OPTIONS_DEFAULT=	DOCS WEBUI
OPTIONS_SUB=	yes
WEBUI_DESC=	Install the separate HTTP/WebSocket connector
WEBUI_RUN_DEPENDS=	${PYTHON_PKGNAMEPREFIX}aiohttp>=3.11:www/py-aiohttp@${PY_FLAVOR}
LINK_DESC=	Install the optional public-key shared-board connector
LINK_RUN_DEPENDS=	${PYTHON_PKGNAMEPREFIX}cryptography>=43:security/py-cryptography@${PY_FLAVOR}

ZMODEM_DESC=	Depend on lrzsz 0.13.1 or newer for external file transfers
ZMODEM_RUN_DEPENDS=	lrzsz>=0.13.1:comms/lrzsz

.include <bsd.port.options.mk>
.if ${PORT_OPTIONS:MWEBUI}
USERS+=		liner_notes_web
GROUPS+=	liner_notes_web
.endif

# Public executables in sbin/bin; support files in DATADIR.
do-install:
	${MKDIR} ${STAGEDIR}${DATADIR}
	${INSTALL_DATA} ${WRKSRC}/LICENSE ${STAGEDIR}${DATADIR}/LICENSE
	${INSTALL_DATA} ${WRKSRC}/THIRD_PARTY.md ${STAGEDIR}${DATADIR}/THIRD_PARTY.md
	${MKDIR} ${STAGEDIR}${PREFIX}/bin ${STAGEDIR}${PREFIX}/sbin ${STAGEDIR}${PREFIX}/libexec
	${MKDIR} ${STAGEDIR}${DATADIR}/scripts
	${INSTALL_PROGRAM} ${WRKSRC}/runtime ${STAGEDIR}${PREFIX}/sbin/liner-notes
	${INSTALL_PROGRAM} ${WRKSRC}/liner-notes-door-lock ${STAGEDIR}${PREFIX}/libexec/liner-notes-door-lock
.for binary in vaultctl memberctl dbconvert
	${INSTALL_PROGRAM} ${WRKSRC}/${binary} ${STAGEDIR}${PREFIX}/bin/liner-notes-${binary}
.endfor
	${INSTALL_DATA} ${WRKSRC}/scripts/*.py ${STAGEDIR}${DATADIR}/scripts/
	${INSTALL_DATA} ${WRKSRC}/webui/bbs_transport.py ${STAGEDIR}${DATADIR}/scripts/bbs_transport.py
	${INSTALL_DATA} ${WRKSRC}/ports/test-resume.py ${STAGEDIR}${DATADIR}/scripts/check-resume.py
	${INSTALL_SCRIPT} ${WRKSRC}/scripts/liner-notes-ssh ${STAGEDIR}${PREFIX}/bin/liner-notes-ssh
.for wrapper in admin doctor maintenance zmodem check-resume serial
	${INSTALL_SCRIPT} ${WRKDIR}/liner-notes-${wrapper} ${STAGEDIR}${PREFIX}/bin/
.endfor
	${MKDIR} ${STAGEDIR}${ETCDIR} ${STAGEDIR}${DATADIR}/menus ${STAGEDIR}${DATADIR}/help/ja ${STAGEDIR}${DATADIR}/help/en
	${INSTALL_DATA} ${WRKDIR}/doors.conf ${STAGEDIR}${ETCDIR}/doors.conf.sample
	${INSTALL_SCRIPT} ${WRKSRC}/scripts/door_guess.py ${STAGEDIR}${DATADIR}/scripts/door_guess.py
	${INSTALL_SCRIPT} ${WRKSRC}/scripts/door_clans.sh ${STAGEDIR}${DATADIR}/scripts/door_clans.sh
	${INSTALL_DATA} ${WRKSRC}/limits.example.conf ${STAGEDIR}${ETCDIR}/limits.conf.sample
	${INSTALL_DATA} ${WRKSRC}/sysinfo.txt ${STAGEDIR}${DATADIR}/sysinfo.txt.sample
	${INSTALL_DATA} ${WRKSRC}/goodbye.txt ${STAGEDIR}${DATADIR}/goodbye.txt.sample
.for asset in welcome systemnews birthday greeting-06 greeting-12 greeting-18
	${INSTALL_DATA} ${WRKSRC}/${asset}.txt ${STAGEDIR}${DATADIR}/${asset}.txt.sample
.endfor
	${INSTALL_DATA} ${WRKSRC}/banner.txt ${STAGEDIR}${DATADIR}/banner.txt.sample
	${INSTALL_DATA} ${WRKDIR}/liner_notes.conf ${STAGEDIR}${ETCDIR}/liner_notes.conf.sample
	${INSTALL_DATA} ${WRKSRC}/vault-limits.example.conf ${STAGEDIR}${ETCDIR}/vault-limits.conf.sample
.for lang in ja en
.for section in board reader settings profile admin sigop
	${INSTALL_DATA} ${WRKSRC}/menus/${section}-${lang}.txt ${STAGEDIR}${DATADIR}/menus/${section}-${lang}.txt.sample
.endfor
	${INSTALL_DATA} ${WRKSRC}/menus/${lang}.txt ${STAGEDIR}${DATADIR}/menus/${lang}.txt.sample
.for topic in adminmembers adminboards adminfiles adminconnections adminstorage account admin chat content draft editor files gather index mail post read settings sigop unread profile
	${INSTALL_DATA} ${WRKSRC}/help/${lang}/${topic}.txt ${STAGEDIR}${DATADIR}/help/${lang}/${topic}.txt.sample
.endfor
.endfor
	${MKDIR} ${STAGEDIR}/var/db/liner-notes/spool ${STAGEDIR}/var/log/liner-notes

do-install-WEBUI-on:
	${PYTHON_CMD} ${WRKSRC}/webui/install.py --destdir ${STAGEDIR} --prefix ${PREFIX} --python ${PYTHON_CMD} --samples-only
	${MKDIR} ${STAGEDIR}/var/db/liner-notes-web/spool

do-install-LINK-on:
	${INSTALL_SCRIPT} ${WRKDIR}/liner-notes-link ${STAGEDIR}${PREFIX}/bin/
	${INSTALL_DATA} ${WRKSRC}/link.example.toml ${STAGEDIR}${ETCDIR}/link.toml.sample
	${INSTALL_DATA} ${WRKSRC}/link-accounts.example.conf ${STAGEDIR}${ETCDIR}/link-accounts.conf.sample

do-install-DOCS-on:
	${MKDIR} ${STAGEDIR}${DOCSDIR}
	cd ${WRKSRC}/docs/book && ${COPYTREE_SHARE} . ${STAGEDIR}${DOCSDIR}

.include <bsd.port.mk>
