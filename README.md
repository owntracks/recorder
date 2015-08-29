# OwnTracks Recorder

## `recorder`

## `ocat`

The _ocat_ utility prints data from the _storage_ which is updated by the _recorder_, accessing it directly via the file system (not via the _recorder_'s REST API). _ocat_ has a daunting number of options, some combinations of which make no sense at all.

Some example uses we consider useful:

* `ocat --list`
   show which uers are in _storage_.
* `ocat --list --user jjolie`
   show devices for the specified user
* `ocat --user jjolie --device ipad`
   print JSON data for the user's device produced during the last 6 hours.
* `ocat ... --format csv`
   produces CSV. Limit the fields you want extracted with `--fields lat,lon,cc` for example.
* `ocat ... --limit 10`
   prints data  for the current month, starting now and going backwards; only 10 locations will be printed. Generally, the `--limit` option reads the storage back to front which makes no sense in some combinations.


## Design decisions

We took a number of decisions for the design of the recorder and its utilities:

* Flat files. The filesystem is the database. Period. That's were everything is stored. It makes incremental backups, purging old data, manipulation via the Unix toolset easy. (Admittedly, for fast lookups you can employ Redis as a cache, but the final word is in the filesystem.) We considered all manner of databases and decided to keep this as simple and lightweight as possible.
* Storage format is typically JSON because it's extensible. If we add an attribute to the JSON published by our apps, you have it right there. There's one slight exception: the monthly logs have a leading timestamp and a relative topic; see below.
* File names are lower case. A user called `JaNe` with a device named `myPHONe` will be found in a file named `jane/myphone`.
* All times are UTC (a.k.a. Zulu or GMT). We got sick and tired of converting stuff back and forth. It is up to the consumer of the data to convert to localtime if need be.
* `ocat`, the _cat_ program for the _recorder_ uses the same back-end which is used by the API though it accesses it directly (i.e. without resorting to HTTP).

## Storage

As mentioned earlier, data is stored in files, and these files are relative to `STORAGEDIR` (compiled into the programs or specified as an option). In particular, the following directory structure can exist, whereby directories are created as needed by the _recorder_:

* `cards/`, optional, contain user cards.
* `ghash/`, unless disabled, reverse Geo data is collected in files named after the geohash in directories named with the first three characters of the geohash.
* `last/` contains the last location publish from a device. E.g. Jane's last publish from her iPhone would be in `last/jjolie/iphone/jjolie-iphone.json`.
* `monitor` a file which contains a timestamp and the last received topic (see Monitoring below).
* `msg/` contains messages received by the Messaging system.
* `photos/` optional; contains the binary photos from a _card_.
* `rec/` the recorder data proper. One subdirectory per user, one subdirectory therein per device. Data files are named `YYYY-MM.rec` (e.g. `2015-08.rec` for the data accumulated during the month of August 2015.




## Requirements

* [hiredis](https://github.com/redis/hiredis) unless `HAVE_REDIS` is false.

## Installation

1. Copy `config.h.example` to `config.h`
1. Edit `config.h`
2. Edit `config.mk` and select features
2. Type `make`

## Reverse Geo

If not disabled with option `-G`, the _recorder_ will attempt to perform a reverse-geo lookup on the location coordinates it obtains. This is stored in Redis (ghash:xxx) if available or in files. If a lookup is not possible, for example because you're over quota, the service isn't available, etc., _recorder_ keeps tracks of the coordinates which could *not* be resolved in a `missing` file:

```
$ cat store/ghash/missing
u0tfsr3 48.292223 8.274535
u0m97hc 46.652733 7.868803
...
```

This can be used to subsequently obtain said geo lookups.


## Monitoring

In order to monitor the _recorder_, whenever an MQTT message is received, the _recorder_ will add an epoch timestamp and the last received topic a Redis key (if configured) or a file otherwise. The Redis key looks like this:

```
redis 127.0.0.1:6379> hgetall ot-recorder-monitor
1) "time"
2) "1439738692"
3) "topic"
4) "owntracks/jjolie/ipad"
```

The `monitor` file is located relative to STORE and contains a single line, the epoch timestamp at the moment of message reception and the topic separated from eachother by a single space:

```
1439738692 owntracks/jjolie/ipad
```

## `ocat`

_ocat_ is a CLI driver for _recorder_: it prints data stored by the _recorder_ in a variety of output formats.

#### Environment

The following environment variables control _ocat_'s behaviour:

* `OCAT_FORMAT` can be set to the preferred output format. If unset, JSON is used. The `--format` option overrides this setting.
* `OCAT_USERNAME` can be set to the preferred username. The `--user` option overrides this environment variable.
* `OCAT_DEVICE` can be set to the preferred device name. The `--device` option overrides this environment variable.
