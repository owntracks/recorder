# API

The Recorder's API provides most of the functions that are surfaced by _ocat_. GET and POST requests are supported, and if a username and device are needed, these can be passed in via `X-Limit-User` and `X-Limit-Device` headers alternatively to GET or POST parameters. (From and To dates may also be specified as `X-Limit-From` and `X-Limit-To` respectively.)

The API endpoint is at `/api/0` and is followed by the verb.

## `monitor`

Returns the content of the `monitor` file as plain text.

```
curl 'http://127.0.0.1:8083/api/0/monitor'
1441962082 owntracks/jjolie/phone
```

## `last`

Returns a list of last users' positions. (Can be limited by _user_, _device_, and _fields_, a comma-separated list of fields which should be returned instead of the default of all fields.)

```
curl http://127.0.0.1:8083/api/0/last [-d user=jjolie [-d device=phone]]
```

```
curl 'http://127.0.0.1:8083/api/0/last?fields=tst,tid,addr,topic,isotst'
```

## `list`

List users. If _user_ is specified, lists that user's devices. If both _user_ and _device_ are specified, lists that device's `.rec` files.

## `locations`

Here comes the actual data. This lists users' locations and requires both _user_ and _device_. Output format is JSON unless a different _format_ is given (`csv`, `json`, `geojson`, `geojsonpoi`, `xml`, and `linestring` are supported).

In order to limit the number of records returned, use _limit_ which causes a reverse search through the `.rec` files; this can be used to find the last N positions.

Date/time ranges may be specified as _from_ and _to_ with dates/times specified as described for _ocat_ above.

```
curl http://127.0.0.1:8083/api/0/locations -d user=jpm -d device=5s
curl http://127.0.0.1:8083/api/0/locations -d user=jpm -d device=5s -d limit=1
curl http://127.0.0.1:8083/api/0/locations -d user=jpm -d device=5s -d format=geojson
curl http://127.0.0.1:8083/api/0/locations -d user=jpm -d device=5s -d from=2014-08-03
curl 'http://127.0.0.1:8083/api/0/locations?from=2015-09-01&user=jpm&device=5s&fields=tst,tid,addr,isotst'
```

## `q`

Query the geo cache for a particular _lat_ and _lon_.

```
curl 'http://127.0.0.1:8083/api/0/q?lat=48.85833&lon=2.295'
{
 "cc": "FR",
 "addr": "9 Avenue Anatole France, 75007 Paris, France",
 "tst": 1441984405
}
```

The reported timestamp was the time at which this cache entry was made. Note that this interface queries only -- it does not populate the cache.

## `photo`

Requires GET method and _user_, and will return the `image/png` 40x40px photograph of a user if available in `STORAGEDIR/photos/` or a transparent 40x40png with a black border otherwise.

## `kill`

If support for this is compiled in, this API endpoint allows a client to remove data from _storage_. (Warning: *any* client can do this, as there is no authentication/authorization in the Recorder!)

```
curl 'http://127.0.0.1:8083/api/0/kill?user=ngin&device=ojo'

{
 "path": "s0/rec/ngin/ojo",
 "status": "OK",
 "last": "s0/last/ngin/ojo/ngin-ojo.json",
 "killed": [
  "2015-09.rec",
 ]
}
```
The response contains a list of removed `.rec` files, and file system operations are logged to syslog.

## `version`

Returns a JSON object which contains the Recorder's version string, such as

```json
{ "version": "0.4.7" }
```

