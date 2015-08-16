# recorder
Recorder

## Requirements

* [hiredis](https://github.com/redis/hiredis) unless `HAVE_REDIS` is false.

## Installation

1. Copy `config.h.example` to `config.h`
1. Edit `config.h`
2. Edit `config.mk` and select features
2. Type `make`

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
