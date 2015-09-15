#!/bin/sh

set -e

tempdir=$(mktemp -d /tmp/ot-XXX)

make install DESTDIR=$tempdir

name="ot-recorder"
version=$(awk '{print $NF;}' version.h | sed -e 's/"//g' )
arch=x86_64
debfile="/tmp/${name}_${version}_${arch}.deb"

rm -f "${debfile}"

fpm -s dir \
	-t deb \
	-n ${name} \
	-v ${version} \
	--vendor "OwnTracks.org" \
	-a all \
	--maintainer 'jpmens@gmail.com' \
	-C $tempdir \
	-p ${debfile} \
	-d "libmosquitto1" \
	-d "libcurl3" \
	--post-install etc/debian/postinst \
	usr var

