## Geo fences

Since a version > 0.6.9, Recorder has support for Geofences (irrespective of their use on the OwnTracks devices). In particular, Recorder can read a list of fences from `.otrw` files, and it will monitor a user's position to determine whether the user is transitioning in to or out of a fence, in which case Recorder will invoke a Lua hook (called `transition`).

### `.otrw`

Recorder reads `.otrw` files from `<store>/waypoints/user/device/user-device.otrw` upon startup and loads these into an internal LMDB database. Each waypoint (geo fence) is keyed by `user-device-geohash(lat,lon)` in the LMDB sub table.

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

When Recorder determines that the user's device has entered or left the geo fence, it invokes a user-provided Lua function called `otr_transition`:

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

In addition to the payload as described in the Booklet, recorder enhances the table passed to the Lua function with the following elements:

- `wplat` / `wplon` are the latitude / longitude of the original waypoint definition
