#!/bin/sh

set -e

if [ ! -f version.h ]; then
	echo "$0 must be run from recorder source tree" >&2
	exit 2
fi

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
	-d "liblua5.2" \
	--post-install etc/debian/postinst \
	usr var

