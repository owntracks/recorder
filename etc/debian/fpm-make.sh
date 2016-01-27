#!/bin/sh

set -e

tempdir=$(mktemp -d /tmp/ot-XXX)

make install DESTDIR=$tempdir

name="ot-recorder"
version=$(awk '{print $NF;}' version.h | sed -e 's/"//g' )
arch=$(uname -m)
debfile="/tmp/${name}_${version}_${arch}.deb"

rm -f "${debfile}"

fpm -s dir \
        -t deb \
        -n ${name} \
        -v ${version} \
        --vendor "OwnTracks.org" \
        -a native \
        --maintainer 'jpmens@gmail.com' \
        --description "A lightweight back-end for consuming OwnTracks data from an MQTT broker" \
        --license "https://github.com/owntracks/recorder/blob/master/LICENSE" \
        --url "http://owntracks.org" \
        -C $tempdir \
        -p ${debfile} \
        -d "libcurl3" \
        -d "libmosquitto1" \
        -d "liblua5.2-0" \
	-d "libsodium13" \
        --post-install etc/debian/postinst \
        usr var
