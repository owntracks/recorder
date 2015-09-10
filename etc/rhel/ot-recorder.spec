%define version 0.0.1

Name:           ot-recorder
Version:        %{version}
Release:        0
Summary:        OwnTracks Recorder
Group:          Applications/Location
License:        MIT
URL:            http://owntracks.org/
Vendor:         OwnTracks team@owntracks.org
Source:         https://github.com/owntracks/recorder
Packager: 	jpmens
Requires:	libmosquitto1
Requires:	libcurl
BuildRequires:	libmosquitto-devel
BuildRequires:	libcurl-devel
BuildRoot:      %{_tmppath}/%{name}-root

%description
OwnTracks is a location-based service which runs over MQTT. The
OwnTracks Recorder connects to an MQTT broker and stores location
publishes from the OwnTracks apps (iOS, Android) into files which
can be viewed through the supporting ocat utility and via a REST
API provided by the Recorder itself.

%build
rm -rf recorder
git clone https://github.com/owntracks/recorder.git
cd recorder
cp config.mk.in config.mk
sed -i -e 's|^STORAGEDEFAULT.*|STORAGEDEFAULT="/var/spool/owntracks/store"|' config.mk
cp config.h.example config.h
make

%install
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_sbindir}
cd recorder
install --strip --mode 0755 ot-recorder %{buildroot}%{_sbindir}
install --strip --mode 0755 ocat %{buildroot}%{_bindir}
mkdir -p %{buildroot}/var/spool/owntracks/store/ghash
mkdir -p %{buildroot}%{_sysconfdir}/default
mkdir -p %{buildroot}%{_sysconfdir}/init.d
cp etc/rhel/ot-recorder.sysconfig %{buildroot}%{_sysconfdir}/default/ot-recorder
install --mode 0755 etc/rhel/ot-recorder.init %{buildroot}%{_sysconfdir}/init.d/ot-recorder

mkdir -p %{buildroot}/var/spool/owntracks/docroot
cp -R wdocs/* %{buildroot}/var/spool/owntracks/docroot

# Build a manifest of the RPM's directory hierarchy.
echo "%%defattr(-, root, root)" >MANIFEST
(cd %{buildroot}; find . -type f -or -type l | sed -e s/^.// -e /^$/d) >>MANIFEST

%clean
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf $RPM_BUILD_ROOT MANIFEST

# %defattr(-,root,owntracks)
%files -f recorder/MANIFEST
%config(noreplace) /var/spool/owntracks/store
%config(noreplace) /etc/default/ot-recorder

%changelog

* Thu Sep 09 2015 Jan-Piet Mens <jpmens@gmail.com>
- Add owntracks group and chmod to SGID for ocat to be able to open LMDB env

* Thu Sep 09 2015 Jan-Piet Mens <jpmens@gmail.com>
- initial spec with tremendous thank yous to https://stereochro.me/ideas/rpm-for-the-unwilling

%post
getent group owntracks > /dev/null || /usr/sbin/groupadd -r owntracks
chmod 775 /var/spool/owntracks/store/ghash
chgrp owntracks /var/spool/owntracks/store/ghash
chgrp owntracks /usr/bin/ocat /usr/sbin/ot-recorder
chmod 3755 /usr/bin/ocat /usr/sbin/ot-recorder
chkconfig --add ot-recorder
