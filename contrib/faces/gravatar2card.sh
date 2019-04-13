#!/bin/sh

# gravatar2card.sh
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

# Usage: gravatar2card email "full name"
#
# Calculate email hash and obtain a 40x40 PNG from Gravatar
# and convert image to BASE64. Create a JSON payload to be published
# retained to owntracks/username/device/info
#
# You probably want to do this:
#
# gravatar2card.sh email-address  "Jane Jolie" > card.json
# mosquitto_pub -t owntracks/jane/phone/info -r -f card.json
#
# Note: the two commands cannot be piplelined (mosquitto_pub -l)
# because of a bug in mosquitto_pub: https://bugs.eclipse.org/bugs/show_bug.cgi?id=478917
# If you have a newer version it should work fine.


[ $# -ne 2 ] && { echo "Usage: $0 email full-name" >&2; exit 2; }

md5=md5
if [ -x /usr/bin/md5sum ]; then
	md5=/usr/bin/md5sum
fi

email="$1"
fullname="$2"
tmp=$(mktemp /tmp/gravatar.XXXXXX)

hash=$(echo -n "${email}" | tr '[:upper:]' '[:lower:]' | $md5)
url="http://www.gravatar.com/avatar/${hash}?s=40"


curl -s -o $tmp "${url}"

imgdata=$(base64 -i $tmp)
rm -f $tmp

cat <<EndOfFile
{"_type":"card","name":"${fullname}","face":"${imgdata}"}
EndOfFile

