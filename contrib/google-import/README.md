# Google Location History Importer
Reads a JSON export of Google Location History from
[Google Takeout](https://takeout.google.com/settings/takeout) and publishes all of its locations
to MQTT. Useful for importing Google Location History into OwnTracks Recorder.

## How to use
1. Ensure you have python3 installed
1. `pip install paho.mqtt`
1. `python3 import-google.py` for basic instructions.  
    * 'filename' is the records.json in the takeout zip file
    * 'topic' default is "owntracks/<user>/<deviceId>"
    * 'tid' is the 2 letter tracker ID