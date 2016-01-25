#!/bin/sh
# launcher.sh
# This will be started when the container starts

set -e

echo -- "--- BEGIN OWNTRACKS LAUNCHER ---"


mkdir -p /owntracks/recorder/store
mkdir -p /owntracks/recorder/store/last

chown -R owntracks:owntracks /owntracks/recorder
/usr/local/sbin/ot-recorder --initialize

mkdir -p /owntracks/certs

if [ -d /owntracks/certs ]; then
	cd /owntracks/certs

	# We prefer the the environment's (-e) MQTTHOSTNAME value.
	# Note, that generate-CA.sh will also consume $IPLIST and
	# $HOSTLIST, both of which may contain space-separated values.

	host=${MQTTHOSTNAME:=$(hostname)}
	echo "*** Using $host as hostname for server certificate"
	/usr/local/sbin/generate-CA.sh ${host}
	ln -sf ${host}.crt mosquitto.crt
	ln -sf ${host}.key mosquitto.key
	chown mosquitto mosquitto.crt
	chown mosquitto mosquitto.key

fi

# --- for Mosquitto's persistence
mkdir -p /owntracks/mosquitto
chown mosquitto:mosquitto /owntracks/mosquitto

# Prime Mosquitto's configuration in volume if it doesn't yet exist there.
# Mosquitto will launch with that, allowing the admin to modify config
# if necessary/desired.

if [ ! -f /owntracks/mosquitto/mosquitto.conf ]; then
	cp /etc/mosquitto/mosquitto.conf /owntracks/mosquitto/mosquitto.conf
fi
if [ ! -f /owntracks/mosquitto/mosquitto.acl ]; then
	cp /etc/mosquitto/mosquitto.acl /owntracks/mosquitto/mosquitto.acl
fi

exec /usr/bin/supervisord -c /etc/supervisor/conf.d/supervisord.conf
