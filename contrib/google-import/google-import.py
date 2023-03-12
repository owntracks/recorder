## original script by smmr0 as found in the deprecated owncloud/tools repo
## updated to make tid value configurable
## updated to work with new takeout json format

#!/usr/bin/env python3
from datetime import datetime 
import time
import argparse
import json
from paho.mqtt import client, publish

class ProtocolAction(argparse.Action):
	def __call__(self, parser, namespace, value, option_string=None):
		setattr(namespace, self.dest, getattr(client, value))

## Custom type for tid argument to allow only 2 letters
def tid_type(arg_value, pat=re.compile(r"^[A-Za-z]{2}$")):
    if not pat.match(arg_value):
        raise argparse.ArgumentTypeError("invalid value, must be 2 letters (e.g. 'aa')")
    return arg_value


parser = argparse.ArgumentParser(description='Import Google Location History into OwnTracks')
parser.add_argument('-H', '--host', default='localhost', help='MQTT host (localhost)')
parser.add_argument('-p', '--port', type=int, default=1883, help='MQTT port (1883)')
parser.add_argument('--protocol', action=ProtocolAction, default=client.MQTTv31, help='MQTT protocol (MQTTv31)')
parser.add_argument('--cacerts', help='Path to files containing trusted CA certificates')
parser.add_argument('--cert', help='Path to file containing TLS client certificate')
parser.add_argument('--key', help='Path to file containing TLS client private key')
parser.add_argument('--tls-version', help='TLS protocol version')
parser.add_argument('--ciphers', help='List of TLS ciphers')
parser.add_argument('-u', '--username', help='MQTT username')
parser.add_argument('-P', '--password', help='MQTT password')
parser.add_argument('-i', '--clientid', help='MQTT client-ID')
parser.add_argument('-t', '--topic', required=True, help='MQTT topic (e.g. owntracks/<user>/<deviceId>)')
parser.add_argument('--tid', required=True, type=tid_type, help="Two letter Tracker ID you want these associated to (e.g. 'aa')")
parser.add_argument('filename', help='Path to file containing JSON-formatted data from Google Location History exported by Google Takeout')
args = parser.parse_args()

messages = []
with open(args.filename) as lh:
	lh_data = json.load(lh)
	for location in lh_data['locations']:
		location_keys = location.keys()
		payload = {
			'_type': 'location'
		}
		payload['tid'] = args.tid
		if 'timestamp' in location_keys:
			#some of the google timestamps did not include milliseconds.
			time_format = "%Y-%m-%dT%H:%M:%S.%fZ" if '.' in location['timestamp'] else "%Y-%m-%dT%H:%M:%SZ"
			payload['tst'] = int(time.mktime(datetime.strptime(location['timestamp'], time_format).timetuple()))
		if 'latitudeE7' in location_keys:
			payload['lat'] = location['latitudeE7'] / 10000000
		if 'longitudeE7' in location_keys:
			payload['lon'] = location['longitudeE7'] / 10000000
		if 'accuracy' in location_keys:
			payload['acc'] = location['accuracy']
		if 'altitude' in location_keys:
			payload['alt'] = location['altitude']
		messages.append(
			{
				'topic': args.topic,
				'payload': json.dumps(payload),
				'qos': 2
			}
		)
	del lh_data

if args.username != None:
	auth={
		'username': args.username,
		'password': args.password
	}
else:
	auth = None

if args.cacerts != None:
	tls = {
		'ca_certs': args.cacerts,
		'certfile': args.cert,
		'keyfile': args.key,
		'tls_version': args.tls_version,
		'ciphers': args.ciphers
	}
else:
	tls = None

publish.multiple(
	messages,
	hostname=args.host,
	port=args.port,
	client_id=args.clientid,
	auth=auth,
	tls=tls
)
