# Google Location History Importer
Reads a JSON export of Google Location History from
[Google Takeout](https://takeout.google.com/settings/takeout) and publishes all of its locations
to MQTT. Useful for importing Google Location History into OwnTracks Recorder.

## Installation
`google-import.py` depends on [`paho.mqtt`](https://github.com/eclipse/paho.mqtt.python) which you can install with pip:
```
pip install paho.mqtt
```
## Usage
The script will read the `records.json` file from Google Takeout and create messages on your MQTT broker.  At a minimum you need to specific the location of the `records.json` file, your TrackerID (`--tid`) and the topic (`-t` or `--topic`) it should place the messages on.
Example:
```
python3 google-import.py records.json --tid <trackerID> -t owntracks/<user>/<deviceID>
```
Make sure to change `<trackerID>`, `<user>`, and `<deviceID>` to your usecase
