#!/bin/sh

# image2card.sh 
# Copyright (C) 2015-2016 Jan-Piet Mens <jpmens@gmail.com>
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

# Usage: image2card imagefile "full name"
#
# Requires `convert' from ImageMagick.
# Read image, convert to PNG, forcing a 40x40 size and encode
# to BASE64. Create a JSON payload to be published to
# owntracks/username/device/info
#
# You probably want to do this:
#
# image2card.sh filename.jpg "Jane Jolie" > card.json
# mosquitto_pub -t owntracks/jane/phone/info -f card.json
#
# Note: the two commands cannot be piplelined (mosquitto_pub -l)
# because of a bug in mosquitto_pub: https://bugs.eclipse.org/bugs/show_bug.cgi?id=478917
# If you have a newish version it should work fine.


[ $# -ne 2 ] && { echo "Usage: $0 image-file full-name" >&2; exit 2; }
imagefile="$1"
fullname="$2"

imgdata=$(convert "${imagefile}" -resize '40x40!' - | base64)
cat <<EndOfFile
{"_type":"card","name":"${fullname}","face":"${imgdata}"}
EndOfFile

