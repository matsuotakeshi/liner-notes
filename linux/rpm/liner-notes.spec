%global toolchain clang
%if 0%{?rhel} >= 10
%global ln_python python3
%else
%global ln_python python3.11
%endif

Name:           liner-notes
Version:        0.74
Release:        1%{?dist}
Summary:        Liner notes multi-user bulletin board system
License:        GPL-3.0-only
URL:            https://github.com/matsuotakeshi/liner-notes
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  clang
BuildRequires:  make
BuildRequires:  openssl-devel
BuildRequires:  sqlite-devel
BuildRequires:  %{ln_python}
BuildRequires:  systemd-rpm-macros
Requires:       %{ln_python}
Requires(pre):  shadow-utils
%{?systemd_requires}

%description
Native bulletin board with threaded articles, private mail, files, chat and
POSIX Door games. Includes offline administration and a systemd service.
Installing or upgrading this package does not start or restart the service.
Web and signed-link Python dependencies are installed separately; see LINUX.md.

%if 0%{?rhel} >= 10
%package web
Summary: Liner notes Web connector
License: GPL-3.0-only AND MIT
Requires: %{name} = %{version}-%{release}
Requires: python3-aiohttp >= 3.11
%description web
Optional browser interface with a separate service account. HTTPS is provided
by a front-end Web server; defaults listen only on localhost.

%package link
Summary: Liner notes signed shared-board connector
Requires: %{name} = %{version}-%{release}
Requires: python3-cryptography >= 43
%description link
Optional mutually authenticated shared-board connector and local control socket.
Peer keys and board permissions must be configured before starting the service.
%endif

%prep
%setup -q

%build
%make_build CC=clang CFLAGS="%{optflags}" LDFLAGS="%{build_ldflags}" SQLITE_CFLAGS= SQLITE_LIBS=-lsqlite3

%install
/usr/bin/%{ln_python} scripts/install-linux.py --destdir %{buildroot} --prefix %{_prefix} --python /usr/bin/%{ln_python} --unitdir %{_unitdir}
%if 0%{?rhel} >= 10
/usr/bin/%{ln_python} linux/rpm/optional.py %{buildroot} /usr/bin/%{ln_python}
/usr/bin/%{ln_python} linux/rpm/manifest.py %{buildroot} web > rpm-web.list
/usr/bin/%{ln_python} linux/rpm/manifest.py %{buildroot} link > rpm-link.list
%else
# The core EL9 package retains the optional link launcher for venv deployments.
%endif
/usr/bin/%{ln_python} linux/rpm/manifest.py %{buildroot} > rpm-files.list
%if 0%{?rhel} < 10
echo /usr/bin/liner-notes-link >> rpm-files.list
%endif
install -d -m 0700 %{buildroot}/var/lib/liner-notes %{buildroot}/var/log/liner-notes

%check
%make_build CC=clang CFLAGS="%{optflags}" SQLITE_CFLAGS= SQLITE_LIBS=-lsqlite3 test-chat
./test-chat

%pre
getent group liner_notes >/dev/null || groupadd -r liner_notes
getent group liner_notes_linkctl >/dev/null || groupadd -r liner_notes_linkctl
getent passwd liner_notes >/dev/null || useradd -r -g liner_notes -d /var/lib/liner-notes -s /sbin/nologin -c 'Liner notes BBS' liner_notes

%post
# No preset/start/restart: configuration and first administrator come first.
if [ -d /run/systemd/system ]; then systemctl daemon-reload >/dev/null 2>&1 || :; fi

%preun
%systemd_preun liner-notes.service

%postun
if [ -d /run/systemd/system ]; then systemctl daemon-reload >/dev/null 2>&1 || :; fi
# Keep accounts, data, logs and modified configuration after removal.

%if 0%{?rhel} >= 10
%pre web
getent group liner_notes_web >/dev/null || groupadd -r liner_notes_web
getent passwd liner_notes_web >/dev/null || useradd -r -g liner_notes_web -d /var/lib/liner-notes-web -s /sbin/nologin liner_notes_web
%post web
if [ -d /run/systemd/system ]; then systemctl daemon-reload >/dev/null 2>&1 || :; fi
%preun web
%systemd_preun liner-notes-web.service
%postun web
if [ -d /run/systemd/system ]; then systemctl daemon-reload >/dev/null 2>&1 || :; fi

%pre link
getent group liner_notes_linkctl >/dev/null || groupadd -r liner_notes_linkctl
getent passwd liner_notes_link >/dev/null || useradd -r -g liner_notes_linkctl -d /var/lib/liner-notes-link -s /sbin/nologin liner_notes_link
%post link
if [ -d /run/systemd/system ]; then systemctl daemon-reload >/dev/null 2>&1 || :; fi
%preun link
%systemd_preun liner-notes-link.service
%postun link
if [ -d /run/systemd/system ]; then systemctl daemon-reload >/dev/null 2>&1 || :; fi
%endif

%files -f rpm-files.list
%attr(0700,liner_notes,liner_notes) %dir /var/lib/liner-notes
%attr(0700,liner_notes,liner_notes) %dir /var/log/liner-notes

%if 0%{?rhel} >= 10
%files web -f rpm-web.list
%files link -f rpm-link.list
%endif

%changelog
* Wed Nov 04 2026 Liner notes contributors - 0.74-1
- Initial RPM; preserve local configuration and instance data across updates.
