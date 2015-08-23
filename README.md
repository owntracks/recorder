# recorder
Recorder

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
