#!/usr/bin/env python
# -*- coding: utf-8 -*-

# ot-ping.py
# send a "pingping" to the OwnTracks Recorder via MQTT

import paho.mqtt.publish as mqtt  # pip install paho-mqtt
import json
import time
import os

__author__    = 'Jan-Piet Mens <jpmens()gmail.com>'
__copyright__ = 'Copyright 2015 Jan-Piet Mens'

hostname    = 'localhost'
port        = 1883
username    = None
password    = None
tls         = None
qos         = 0
retain      = 0

def pingping():

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
        "tst"       : int(time.time()),
    }

    topic = "nop/ping/ping"
    payload = json.dumps(location)

    mqtt.single(topic, payload,
            auth=auth,
            tls=tls,
            **params)


if __name__ == '__main__':
    pingping()
