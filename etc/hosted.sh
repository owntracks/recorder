#!/bin/sh
# hosted.sh
# Template shell script to use for launching Recorder with a
# Hosted OwnTracks account. Fill in the blanks:

export OTR_USER="________"          # your OwnTracks Hosted username
export OTR_DEVICE="______"          # one of your OwnTracks Hosted device names
export OTR_TOKEN="_____________"    # the Token corresponding to above pair
export OTR_CAFILE="/path/to/startcom-ca-bundle.pem"


ot-recorder --hosted "owntracks/#"
