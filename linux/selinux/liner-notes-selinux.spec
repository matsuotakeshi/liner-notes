%{!?policy_release:%global policy_release 1}
Name:           liner-notes-selinux
Version:        0.1.0
Release:        %{policy_release}%{?dist}
Summary:        Confined SELinux domains for Liner Notes
License:        GPL-3.0-only
BuildArch:      noarch
Source0:        liner_notes.te
Source1:        liner_notes.fc
Source2:        selinux_labels.py
Source3:        LICENSE
BuildRequires:  selinux-policy-devel
BuildRequires:  make
Requires:       selinux-policy-targeted
Requires:       policycoreutils-python-utils
Requires:       python3
Requires(post): policycoreutils
Requires(preun): procps-ng
Requires(postun): policycoreutils

%description
Optional policy with separate core, Web, link, administrator, transfer and
Door domains. Installation does not restart services or change port mappings.
Administrators explicitly apply data labels and service configuration.

%prep
%setup -c -T
cp %{SOURCE0} %{SOURCE1} .
cp %{SOURCE3} LICENSE

%build
make -f /usr/share/selinux/devel/Makefile liner_notes.pp

%install
install -D -m 0644 liner_notes.pp %{buildroot}%{_datadir}/selinux/packages/liner_notes.pp
install -D -m 0755 %{SOURCE2} %{buildroot}%{_sbindir}/liner-notes-selinux

%post
# Priority 200 does not replace administrator-local priority 400 overrides.
/usr/sbin/semodule -X 200 -i %{_datadir}/selinux/packages/liner_notes.pp || exit 1
# Only package entrypoints. Existing state/config/keys are NOT relabelled here.
for p in /usr/sbin/liner-notes /usr/sbin/liner-notes-web /usr/bin/liner-notes-link \
         /usr/bin/liner-notes-admin /usr/bin/liner-notes-memberctl \
         /usr/bin/liner-notes-vaultctl /usr/bin/liner-notes-dbconvert /usr/bin/liner-notes-zmodem; do
    if [ -f "$p" ] && [ ! -L "$p" ]; then /usr/sbin/restorecon "$p" || exit 1; fi
done

%preun
if [ "$1" -eq 0 ] && ps -eZ | grep -Eq ':liner_notes(_(web|link|admin|door|transfer))?_t:'; then
    echo 'Stop Liner Notes confined processes before removing its SELinux policy.' >&2
    exit 1
fi

%postun
if [ "$1" -eq 0 ]; then
    /usr/sbin/semodule -X 200 -r liner_notes || exit 1
    # Do not remove local semanage entries or relabel existing state directories.
    for p in /usr/sbin/liner-notes /usr/sbin/liner-notes-web /usr/bin/liner-notes-link \
             /usr/bin/liner-notes-admin /usr/bin/liner-notes-memberctl \
             /usr/bin/liner-notes-vaultctl /usr/bin/liner-notes-dbconvert /usr/bin/liner-notes-zmodem; do
        if [ -f "$p" ] && [ ! -L "$p" ]; then /usr/sbin/restorecon "$p" || exit 1; fi
    done
fi

%files
%license LICENSE
%{_datadir}/selinux/packages/liner_notes.pp
%{_sbindir}/liner-notes-selinux

%changelog
* Mon Nov 09 2026 Liner Notes contributors - 0.1.0-1
- Explicitly installed separated domains, preserving local mapping and services.
