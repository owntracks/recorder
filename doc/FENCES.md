## Geo fences

Since version > 0.6.9, Recorder has compiled-in support for Geofences irrespective of their use on the OwnTracks devices. (What we mean by this is that OwnTracks iOS/Android apps do native geofences, but if you have some device which just publishes its location in OwnTracks JSON format, you can now process geofences for it with the Recorder.)

In particular, Recorder can read a list of fences from `.otrw` files, and it will monitor a user's position to determine whether the user is transitioning in to or out of a fence, in which case Recorder will invoke a Lua hook (called `otr_transition`) upon detecting such a transition event. This Lua hook function is one which you provide, and it could, say, publish a new message to an MQTT broker, invoke a REST API, etc.

### `.otrw`

Recorder reads `.otrw` files from `<store>/waypoints/user/device/user-device.otrw` upon startup and loads these into an internal LMDB database. Each waypoint (geo fence) is keyed by `user-device-geohash(lat,lon)` in the LMDB sub table. In addition, when Recorder receives a waypoint dump (say, from an OwnTracks device), it will also inspect said dump and merge new waypoints for the user/device into this database.

For example, the following otrw

```json
{
	"_type": "waypoints",
	"waypoints": [
		{
			"_type": "waypoint",
			"tst": 9999,
			"lat": 48.85833,
			"lon": 3.29513,
			"rad": 1000,
			"desc": "chez Madelaine"
		}
	]
}
```

is read in as 

```
$ ocat -S jp --dump=wp
uno-lua-u0dmfyrkqh {"lat":48.85833,"lon":3.29513,"rad":1000,"desc":"chez Madelaine","io":false}
```

Note how the `io` (in / out) element in the JSON indicates whether the last position reported was in or out of the fence.

### Transition hook

When Recorder determines that the user's device has entered or left the geo fence, it invokes a user-provided Lua function called `otr_transition()`:

```lua
function otr_init()
end

function otr_exit()
end

function otr_transition(topic, _type, data)
	print("IN TRANSITION " .. _type .. " " .. topic)
	otr.publish('special/topic', data['event'] .. " " .. data['desc'])
end
```

`topic` is the topic on which the message was originally received, `_type` is `"transition"` and `data` is a Lua table with the full payload plus merged data for the event.

In addition to the payload as described in the Booklet, recorder enhances the table passed to the Lua function with the following elements:

- `wplat` / `wplon` are the latitude / longitude of the original waypoint definition
