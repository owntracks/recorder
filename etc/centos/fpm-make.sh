#!/bin/sh

set -e

# export PACKAGEDIR=/media/sf_proxmox/centos/repo

PACKAGEDIR=${PACKAGEDIR:=/tmp}

tempdir=$(mktemp -d /tmp/ot-XXX)

make install DESTDIR=$tempdir

name="ot-recorder"
version=$(awk '{print $NF;}' version.h | sed -e 's/"//g' )
arch=$(uname -m)
rpmfile="${PACKAGEDIR}/${name}_${version}_${arch}.rpm"

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
        --post-install etc/centos/postinst \
        usr var
