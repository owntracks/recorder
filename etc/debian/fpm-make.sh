#!/bin/sh

set -e

tempdir=$(mktemp -d /tmp/ot-XXX)

make install DESTDIR=$tempdir

name="ot-recorder"
# add -0 to indicate "not in Debian" as per Roger's suggestion
version="$(awk '{print $NF;}' version.h | sed -e 's/"//g' )-0"
arch=$(uname -m)

case $arch in
	armv7l) arch=armhf;;
esac

debfile="${name}_${version}_${arch}.deb"

rm -f "${debfile}"

fpm -s dir \
        -t deb \
        -n ${name} \
        -v ${version} \
        --vendor "OwnTracks.org" \
        -a "${arch}" \
        --maintainer 'jpmens@gmail.com' \
        --description "A lightweight back-end for consuming OwnTracks data from an MQTT broker" \
        --license "https://github.com/owntracks/recorder/blob/master/LICENSE" \
        --url "http://owntracks.org" \
        -C $tempdir \
        -p ${debfile} \
        -d "libcurl3" \
        -d "libmosquitto1" \
        -d "liblua5.2-0" \
        -d "libconfig9" \
	--config-files etc/default/ot-recorder \
        --post-install etc/debian/postinst \
        usr var etc

echo "${debfile}" > package.name
rm -rf "${tempdir}"
