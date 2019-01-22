#!/bin/sh

set -e

tempdir=$(mktemp -d /tmp/ot-XXX)

make install DESTDIR=$tempdir

mkdir -p $tempdir/usr/share/doc/ot-recorder
install -D README.md $tempdir/usr/share/doc/ot-recorder/README.md
install -D etc/ot-recorder.service $tempdir/usr/share/doc/ot-recorder/ot-recorder.service


name="ot-recorder"
version=$(awk 'NR==1 {print $NF;}' version.h | sed -e 's/"//g' )
arch=$(uname -m)
rpmfile="${name}_${version}_${arch}.rpm"

rm -f "${rpmfile}"

fpm -s dir \
        -t rpm \
        -n ${name} \
        -v ${version} \
        --vendor "OwnTracks.org" \
        -a native \
        --maintainer 'jpmens@gmail.com' \
        --description "A lightweight back-end for consuming OwnTracks data from an MQTT broker" \
        --license "https://github.com/owntracks/recorder/blob/master/LICENSE" \
        --url "http://owntracks.org" \
        -C $tempdir \
        -p ${rpmfile} \
        -d "libcurl" \
        -d "libmosquitto1" \
        -d "lua" \
        -d "libconfig" \
        -d "lmdb" \
	--config-files etc/default/ot-recorder \
        --post-install etc/centos/postinst \
        usr var etc

echo "${rpmfile}" > package.name
rm -rf "${tempdir}"
