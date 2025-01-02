#!/bin/sh

# image2card.sh 
# Copyright (C) 2015-2025 Jan-Piet Mens <jpmens@gmail.com>
# 
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

# Usage: image2card imagefile "full name" tid
#
# Requires `convert' from ImageMagick.
# Read image, convert to JPG, forcing a 192x192 size and encode
# to BASE64. Create a JSON payload to be published to
# owntracks/username/device/info
#
# You probably want to do this:
#
# image2card.sh filename.jpg "Jane Jolie" "JJ" > card.json
# mosquitto_pub -t owntracks/jane/phone/info -f card.json -r -q 2
#
# Note: the two commands cannot be piplelined (mosquitto_pub -l)
# because of a bug in mosquitto_pub: https://bugs.eclipse.org/bugs/show_bug.cgi?id=478917
# If you have a newish version it should work fine.


[ $# -ne 3 ] && { echo "Usage: $0 image-file full-name tid" >&2; exit 2; }
imagefile="$1"
fullname="$2"
tid="$3"

base64switch=""
base64check=$(echo "jj" | base64 -w 0 > /dev/null 2>&1)
[ "$?" -eq "0" ] && base64switch="-w 0"
imgdata=$(magick "${imagefile}" -resize '192x192!' JPG:- | base64 $base64switch)

cat <<EndOfFile
{"_type":"card","tid":"${tid}", "name":"${fullname}","face":"${imgdata}"}
EndOfFile

