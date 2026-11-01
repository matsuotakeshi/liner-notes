PORTNAME=	lrzsz
DISTVERSION=	0.13.1
CATEGORIES=	comms
MASTER_SITES=	https://www.ohse.de/uwe/releases/

MAINTAINER=	dinoex@FreeBSD.org
COMMENT=	Receive/Send files via X/Y/ZMODEM protocol (unrestrictive)
WWW=		https://ohse.de/uwe/software/lrzsz.html

LICENSE=	GPLv2+
LICENSE_FILE=	${WRKSRC}/COPYING

USES=		cpe gmake
CPE_VENDOR=	lrzsz_project
GNU_CONFIGURE=	yes
CONFIGURE_ARGS=	--disable-nls
ALL_TARGET=	all

CONFLICTS_INSTALL=	lrzip

# configure defines _POSIX_C_SOURCE, hiding S_IFMT on FreeBSD.
CFLAGS+=	-U_POSIX_C_SOURCE

.include <bsd.port.mk>
