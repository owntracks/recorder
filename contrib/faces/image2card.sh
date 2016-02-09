#!/bin/sh
# image2card.sh (Oct 2015) by Jan-Piet Mens
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
# mosquitto_pub -t owntracks/jane/phone -f card.json
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

