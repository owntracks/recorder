#!/bin/sh

set -e

tempdir=$(mktemp -d /tmp/ot-XXX)

make install DESTDIR=$tempdir

install -D -m644 README.md $tempdir/usr/share/doc/ot-recorder/README.md
install -D -m644 etc/ot-recorder.service $tempdir/etc/systemd/system/ot-recorder.service

name="ot-recorder"
# add -0 to indicate "not in Debian" as per Roger's suggestion
version="$(awk 'NR==1 {print $NF;}' version.h | sed -e 's/"//g' )-0-deb$(cat /etc/debian_version)"

arch=$(uname -m)
case $arch in
	armv7l) arch=armhf;;
esac

debfile="${name}_${version}_${arch}.deb"

rm -f "${debfile}"

libcurl='libcurl3'
libsodium='libsodium13'
liblua='liblua5.2-0'
case $(cat /etc/debian_version) in
	8.8) ;;
	9.*) libsodium="libsodium18" ;;
	10.*)
		libsodium="libsodium23"
		libcurl="libcurl3-gnutls"
		;;
        11.*)
                libsodium="libsodium23"
                libcurl="libcurl4"
                liblua="liblua5.4-0"
                ;;
        12.*)
                libsodium="libsodium23"
                libcurl="libcurl4"
                liblua="liblua5.4-0"
                ;;
esac

fpm -s dir \
        -t deb \
        -n ${name} \
        -v ${version} \
        --vendor "OwnTracks.org" \
        -a "${arch}" \
        --maintainer 'jpmens@gmail.com' \
	--deb-no-default-config-files \
        --description "A lightweight back-end for consuming OwnTracks data from an MQTT broker" \
        --license "https://github.com/owntracks/recorder/blob/master/LICENSE" \
        --url "http://owntracks.org" \
        -C $tempdir \
        -p ${debfile} \
        -d "${libcurl}" \
        -d "libmosquitto1" \
        -d "${liblua}" \
        -d "libconfig9" \
        -d "${libsodium}" \
        -d "liblmdb0" \
        -d "libuuid1" \
	--config-files etc/default/ot-recorder \
        --post-install etc/debian/postinst \
        usr var etc

echo "${debfile}" > package.name
rm -rf "${tempdir}"
