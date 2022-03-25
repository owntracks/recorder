import gpxpy
import json
import time
import datetime
import sys

if len(sys.argv) < 2:
    print("Please pass a .gpx file to parse")
    quit()

gpx_file = open(sys.argv[1], 'r')

print("Parsing ", sys.argv[1], "...", sep="")
gpx = gpxpy.parse(gpx_file)

for track in gpx.tracks:
    for segment in track.segments:
    	# Creates a set of every month and year the points were recorded in
        months = set()
        for point in segment.points:
            months.add(point.time.strftime("%Y-%m"))
            
        # Generates the contents for every month and saves them to a .rec file (eg. 2022-03.rec)
        for month in months:
            output = ""
            for point in segment.points:
                if point.time.strftime("%Y-%m") == month:
                    point_json = {'_type' : point.type, 'lat' : point.latitude, 'lon' : point.longitude, 'tst' : int(point.time.timestamp()), '_http' : True, 'alt' : point.elevation}
                    output += point.time.strftime("%Y-%m-%dT%XZ") + "\t*" + (18 * " ") + "\t" + json.dumps(point_json) + "\n"
            rec_file = open(month + ".rec", "w")
            print("Saving file ", month, ".rec...", sep="")
            rec_file.write(output)
            rec_file.close()

