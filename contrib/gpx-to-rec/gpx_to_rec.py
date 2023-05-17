#!/usr/bin/env python
import gpxpy
import json
import time
import datetime
import sys

recs = []
for file in sys.argv[1:]:
    print(file)
    gpx_file = open(file, 'r')
    gpx = gpxpy.parse(gpx_file)
    for track in gpx.tracks:
        for segment in track.segments:
            for point in segment.points:
                point_json = {'_type' : 'location', 'lat' : point.latitude, 'lon' : point.longitude, 'tst' : int(point.time.timestamp()), '_http' : True, 'alt' : point.elevation}
                recs.append(point.time.strftime("%Y-%m-%dT%XZ") + "\t*" + (18 * " ") + "\t" + json.dumps(point_json))

recs.sort()
for i in recs:
    ts = i.split("\t")[0]
    month = ts[0:7]
    with open(month + '.rec', 'a') as f:
        f.write(i + "\n")
