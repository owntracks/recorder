#!/usr/bin/env python
# -*- coding: utf-8 -*-

# OwnTracks Recorder
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

# ot-ping.py
# send a "pingping" to the OwnTracks Recorder via MQTT with "now"
# in timestamp (tst). Then verify via Recorder's REST interface
# whether the timestamp matches. Print a Nagios/Icinga-compatible
# string to stdout and exit() appropriately.

import paho.mqtt.publish as mqtt  # pip install paho-mqtt
import json
import time
import os
import sys
import requests

__author__    = 'Jan-Piet Mens <jpmens()gmail.com>'
__copyright__ = 'Copyright 2015 Jan-Piet Mens'

hostname    = 'localhost'
port        = 1883
username    = None
password    = None
tls         = None
qos         = 0
retain      = 0

UNKNOWN = -1
OK = 0
WARNING = 1
CRITICAL = 2

codes = [ 'OK', 'WARNING', 'CRITICAL' ]

def pingping(tics):
    status = OK
    msg = "MQTT publish"

    params = {
            'hostname'  : hostname,
            'port'      : port,
            'qos'       : qos,
            'retain'    : retain,
            'client_id' : "ot-recorder-ping-ping-%s" % os.getpid(),
    }
    auth = None

    if username is not None:
        auth = {
            'username' : username,
            'password' : password
        }

    topic = "owntracks/ping/ping"

    location = {
        "_type"     : "location",
        "tid"       : "pp",
        "lat"       : 51.47879,
        "lon"       : -0.010677,
        "tst"       : tics,
    }

    topic = "owntracks/ping/ping"
    payload = json.dumps(location)

    try:
        mqtt.single(topic, payload,
                auth=auth,
                tls=tls,
                **params)
    except Exception, e:
        status = CRITICAL
        msg = msg + " " + str(e)

    return status, msg

# Connect to URL (http://host:port) and obtain the last position
# of the ping/ping user. Note: URL must not include the API endpoint
# Verify, that the `tst' in the returned payload is close to `tics'

def check_response(url, tics):
    status = OK
    msg = "ot-recorder pingping at %s: " % url
    data = None

    try:
        r = requests.post(url + "/api/0/last", params= { 'user' : 'ping', 'device' : 'ping' })
        data = json.loads(r.text)[0]    # Return is an array
    except Exception, e:
        return CRITICAL, str(e)

    tst = data['tst']
    diff = tics - tst

    if diff > 10:
        status = WARNING
    if diff > 60:
        status = CRITICAL

    msg = msg + " %d seconds difference" % diff
    return status, msg

if __name__ == '__main__':
    tics = int(time.time())

    status, msg = pingping(tics)
    if status != OK:
        print "%s ot-recorder pingping failed: %s" % (codes[status], msg)
        sys.exit(status)

    status, msg = check_response('http://127.0.0.1:8083', tics)

    print "%s %s" % (codes[status], msg)
    sys.exit(status)

