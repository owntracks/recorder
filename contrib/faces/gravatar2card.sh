#!/bin/sh
# gravatar2card.sh (Feb 2017) by Jan-Piet Mens
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

email="$1"
fullname="$2"
tmp=$(mktemp /tmp/gravatar.XXXXXX)

hash=$(echo -n "${email}" | tr '[:upper:]' '[:lower:]' | md5)
url="http://www.gravatar.com/avatar/${hash}?s=40"


curl -s -o $tmp "${url}"

imgdata=$(base64 -i $tmp)
rm -f $tmp

cat <<EndOfFile
{"_type":"card","name":"${fullname}","face":"${imgdata}"}
EndOfFile

