#!/usr/bin/env python

import geocoder
import json
import sys
import fileinput
import time

def reverse(lat, lon):
    ''' return JSON payload for ghash database from lat, lon '''

    try:
        g = geocoder.google([lat, lon], method='reverse')

        # addr = '%s %s, %s %s, %s' % (g.street_long, g.housenumber, g.postal, g.city, g.country_long)
        addr = g.address
    
        data = {
            'tst'       : int(time.time()),
            'cc'        : g.country,
            'addr'      : addr,
            'locality'  : g.city_long
        }

        return json.dumps(data)
    except Exception, e:
        print >> sys.stderr, "lookup failed:", str(e)
        return None

for line in fileinput.input():
    line = line.rstrip()
    ghash, lat, lon = line.split()

    f = open("missing.again", "w")
    
    payload = reverse(lat, lon)
    if payload is None:
        f.write("%s\n" % line)
        continue

    time.sleep(1)
    print ghash, payload
