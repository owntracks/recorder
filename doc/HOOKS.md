# Lua hooks

If Recorder is compiled with Lua support, a Lua script you provide is launched at startup. Lua is _a powerful, fast, lightweight, embeddable scripting language_. You can use this to process location publishes in any way you desire: your imagination (and Lua-scripting knowhow) set the limits. Some examples:

* insert publishes into a database of your choice
* switch on the coffee machine when your OwnTracks device reports you're entering home (but see also [mqttwarn](http://jpmens.net/2014/02/17/introducing-mqttwarn-a-pluggable-mqtt-notifier/))
* write a file with data in a format of your choice (see `etc/example.lua`)

Run the Recorder with the path to your Lua script specified in its `--lua-script` option (there is no default). If the script cannot be loaded (e.g. because it cannot be read or contains syntax errors), the Recorder unloads Lua and continues *without* your script.

If the Lua script can be loaded, it is automatically provided with a table variable called `otr` which contains the following members:

* `otr.version` is a read-only string with the Recorder version (example: `"0.3.2"`)
* `otr.log(s)` is a function which takes a string `s` which is logged to syslog at the Recorder's facility and log level INFO.
* `otr.strftime(fmt, t)` is a function which takes a format string `fmt` (see `strftime(3)`) and an integer number of seconds `t` and returns a string with the formatted UTC time. If `t` is 0 or negative, the current system time is used.
* `otr.putdb(key, value)` is a function which takes two strings `k` and `v` and stores them in the named LMDB database called `luadb`. This can be viewed with
* `otr.getdb(key)` is a function which takes a single string `key` and returns the database value associated with that key or `nil` if the key isn't stored.

```
ocat --dump=luadb
```

Your Lua script *must* provide the following functions:

## `otr_init`

This is invoked at start of Recorder. If the function returns a non-zero value, Recorder unloads Lua and disables its processing; i.e. the `hook()` will *not* be invoked on location publishes.

## `otr_exit`

This is invoked when the Recorder stops, which it doesn't really do unless you CTRL-C it or send it a SIGTERM signal.


## `otr_hook`

This function is invoked at every location publish processed by the Recorder. Your function is passed three arguments:

1. _topic_ is the topic published to (e.g. `owntracks/jane/phone`)
2. _type_ is the type of MQTT message. This is the `_type` in our JSON messages (e.g. `location`, `cmd`, `transition`, ...) or `"unknown"`.
3. _location_ is a [Lua table](http://www.lua.org/pil/2.5.html) (associative array) with all the elements obtained in the JSON message. In the case of _type_ being `location`, we also add country code (`cc`) and the location's address (`addr`) unless reverse-geo lookups have been disabled in Recorder.

Assume the following small example Lua script in `example.lua`:

```lua
local file

function otr_init()
	otr.log("example.lua starting; writing to /tmp/lua.out")
	file = io.open("/tmp/lua.out", "a")
	file:write("written by OwnTracks Recorder version " .. otr.version .. "\n")
end

function otr_hook(topic, _type, data)
	local timestr = otr.strftime("It is %T in the year %Y", 0)
	print("L: " .. topic .. " -> " .. _type)
	file:write(timestr .. " " .. topic .. " lat=" .. data['lat'] .. data['addr'] .. "\n")
end

function otr_exit()
end
```

When Recorder is launched with `--lua-script example.lua` it invokes `otr_init()` which opens a file. Then, for each location received, it calls `otr_hook()` which updates the file.

Assuming an OwnTracks device publishes this payload

```json
{"cog":-1,"batt":-1,"lon":2.29513,"acc":5,"vel":-1,"vac":-1,"lat":48.85833,"t":"u","tst":1441984413,"alt":0,"_type":"location","tid":"JJ"}
```

the file `/tmp/lua.out` would contain

```txt
written by OwnTracks Recorder version 0.3.0
It is 14:10:01 in the year 2015 owntracks/jane/phone lat=48.858339 Avenue Anatole France, 75007 Paris, France
```

## `otr_putrec`

An optional function you provide is called `otr_putrec(u, d, s)`. If it exists,
it is called with the current user in `u`, the device in `d` and the payload
(which for OwnTracks apps is JSON but for, eg Greenwich devices might not be) in the string `s`. If your function returns a
non-zero value, the Recorder will *not* write the REC file for this publish.

## `otr_httpobject`

An optional function you provide is called `otr_httpobject(u, d, t, data)` where `u` is the username used by the client (`?u=`), `d` is the device name (`&d=` in the URI), `t` is the OwnTracks JSON `_type` and `data` a Lua table built from the OwnTracks JSON payload of `_type`. If it exists, this function is called whenever a POST is received in httpmode and the Recorder is gathering data to return to the client app. The function *must* return a Lua table containing any number of string, number, or boolean values which are converted to a JSON object and appended to the JSON array returned to the client. An [example](etc/example.lua) shows how, say, a transition event can be used to open the Featured content tab in the app.

## `otr_transition`

See [geo fences](FENCES.md).

## `otr_revgeo`

If the user-defined `otr_revgeo()` function exists in the Lua script, it is invoked by the Recorder to obtain reverse-geo data on a publish. (Unless Recorder was launched with `--norevgeo` in which case no such information is gathered at all.)

`otr_revgeo()` overrides the built-in Google reverse-geo lookups. So, to clarify, if this function is defined, none of the default reverse-geo lookups will be attempted.

The function is invoked with _topic_, _user_, _device_, _lat_, and _lon_, and it should return a table with at least `cc` and `addr` populated. Any other elements in the table are added and passed on to, say, the Websocket interface. If the element `_rec` is defined in the table and its value is _true_, the data will be merged into the payload stored in the REC file.

```lua
function otr_revgeo(topic, user, device, lat, lon)

        local d = {}

        d['cc']         = 'JP'
        d['addr']       = 'Some place or other'
        d['beverage']   = 'tea'

        d['_rec']       = true

        return d
end
```

Running the recorder with the above function (and `_rec` set) could cause it to store the following in a REC file:

```json
{"_type":"location","cog":16,"batt":11,"lat":48.85833,"lon":3.29513,"acc":5,"vel":12,"alt":0,"tid":"JJ","_geoprec":3,"tst":1495010110,"addr":"Some place or other","cc":"JP","beverage":"tea"}
```

The name of the Lua function defaults to `otr_revgeo` but can be modified/set on a per/payload basis by populating the JSON element `_lua` before the payload reaches the recorder. The recorder will use the string contained in that element as name of the Lua function. So, assuming you have a Lua function called `geo2` defined in your Lua script and you wish that to be used, the JSON sent to the Recorder would look like 

```json
{"_type":"location","lat":48.85833,"lon":3.29513,"tid":"JJ","_lua":"geo2","tst":1495018728}
```

## Hooklets

After running `otr_hook()`, the Recorder attempts to invoke a Lua function for each of the elements in the extended JSON. If, say, your Lua script contains a function called `hooklet_lat`, it will be invoked every time a `lat` is received as part of the JSON payload. Similarly with `hooklet_addr`, `hooklet_cc`, `hooklet_tst`, etc. These _hooklets_ are invoked with the same parameters as `otr_hook()`.

You define a hooklet function only if you're interested in expressly triggering on a particular JSON element.

In addition to compiling with Lua support, if Recorder is built with MQTT support, a function `otr_publish()` is surfaced into your Lua script.

```
otr_publish(topic, payload, qos, retain)
```

`topic` and `payload` are mandatory and must be strings. `qos` and `retain` are optional and specify the QoS for publishing as well as a retain flag; `qos` must be specified if `retain` is to be.

```lua
JSON = (loadfile "JSON.lua")() -- http://regex.info/blog/lua/json

function otr_init()
end

function otr_exit()
end

function otr_hook(topic, _type, data)
	-- republish partial messages to a different topic
	local d = {}
	d['_type']      = data['_type']
	d['lat']        = data['lat']
	d['lon']        = data['lon']
	d['fromLua']	= true

	local payload = JSON:encode(d)

	otr.publish("topic/1", payload, 1, 0)
end
```

This function allows your code to publish via MQTT from the Recorder, using the same MQTT connection (including TLS, authentication, etc) as the Recorder uses. Note also, that the Recorder's MQTT connection defines the ACL in use.

The following example republishes messages received via HTTP to MQTT:

```lua
JSON = (loadfile "JSON.lua")() -- http://regex.info/blog/lua/json

function otr_init()
end

function otr_exit()
end

function otr_hook(topic, _type, data)
        -- republish http messages to MQTT
        if data['_http'] then
                data["_http"] = nil
                if data["topic"] ~= nil then
                        topic = data['topic']
                        data["tst"] = math.floor(data["tst"])
                        data["_lua"] = true
                        local payload = JSON:encode(data)

                        otr.publish(topic, payload, 1, 0)
                end
        end
end
```
