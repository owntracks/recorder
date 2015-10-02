%define version 0.4.2

Name:           ot-recorder
Version:        %{version}
Release:        0
Summary:        OwnTracks Recorder
Group:          Applications/Location
License:        MIT
URL:            http://owntracks.org/
Vendor:         OwnTracks team@owntracks.org
Source:         https://github.com/owntracks/recorder/archive/%{version}.tar.gz
Packager: 	jpmens
Requires:	libmosquitto1
Requires:	libcurl
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%if %{defined suse_version}
Requires:  libopenssl1_0_0, libcurl
BuildRequires:  libopenssl-devel, libcurl-devel, libmosquitto-devel
%endif

%if %{defined rhel_version}
Requires:   openssl
BuildRequires:  openssl-devel, libcurl-devel, libmosquitto-devel
%endif

%if %{defined centos_version}
Requires:   openssl
BuildRequires:  openssl-devel, libcurl-devel, libmosquitto-devel
%endif

%if %{defined fedora_version}
Requires:   openssl
BuildRequires:  openssl-devel, libcurl-devel, libmosquitto-devel
%endif

%description
OwnTracks is a location-based service which runs over MQTT. The
OwnTracks Recorder connects to an MQTT broker and stores location
publishes from the OwnTracks apps (iOS, Android) into files which
can be viewed through the supporting ocat utility and via a REST
API provided by the Recorder itself.

%prep
%setup -n recorder-%{version}

%build
cp config.mk.in config.mk
make

%install
## make install
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_sbindir} %{buildroot}%{_datadir}/doc/owntracks
mkdir -p %{buildroot}%{_sbindir} %{buildroot}/var/spool/owntracks/recorder/htdocs
cp -R docroot/* %{buildroot}/var/spool/owntracks/recorder/htdocs
install --strip --mode 0755 ot-recorder %{buildroot}%{_sbindir}
install --strip --mode 0755 ocat %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_sysconfdir}/default
mkdir -p %{buildroot}%{_sysconfdir}/init.d
cp etc/rhel/ot-recorder.defaults %{buildroot}%{_sysconfdir}/default/ot-recorder
install --mode 0755 etc/rhel/ot-recorder.init %{buildroot}%{_sysconfdir}/init.d/ot-recorder
install --mode 0444 README.md %{buildroot}%{_datadir}/doc/owntracks
mkdir -p %{buildroot}/var/spool/owntracks/recorder/store

# Build a manifest of the RPM's directory hierarchy.
echo "%%defattr(-, root, root)" >MANIFEST
(cd %{buildroot}; find . -type f -or -type l | sed -e s/^.// -e /^$/d) >>MANIFEST

%clean
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf $RPM_BUILD_ROOT MANIFEST

# %defattr(-,root,owntracks)
%files -f MANIFEST
%config /var/spool/owntracks/recorder/store
%config /etc/default/ot-recorder

%changelog

* Mon Sep 14 2015 Jan-Piet Mens <jpmens@gmail.com>
- munge for OBS (thank you, Roger Light, for your help!)
- relocate spool dir

* Sun Sep 13 2015 Jan-Piet Mens <jpmens@gmail.com>
- remove config.h.example
- use plain config.mk

* Thu Sep 09 2015 Jan-Piet Mens <jpmens@gmail.com>
- Add owntracks group and chmod to SGID for ocat to be able to open LMDB env

* Thu Sep 09 2015 Jan-Piet Mens <jpmens@gmail.com>
- initial spec with tremendous thank yous to https://stereochro.me/ideas/rpm-for-the-unwilling

%post
getent group owntracks > /dev/null || /usr/sbin/groupadd -r owntracks
mkdir -p /var/spool/owntracks/recorder/store/ghash
chmod 775 /var/spool/owntracks/recorder/store/ghash
chgrp owntracks /var/spool/owntracks/recorder/store
chgrp owntracks /var/spool/owntracks/recorder/store/ghash
chgrp owntracks /usr/bin/ocat /usr/sbin/ot-recorder
chmod 3755 /usr/bin/ocat /usr/sbin/ot-recorder
chkconfig --add ot-recorder
