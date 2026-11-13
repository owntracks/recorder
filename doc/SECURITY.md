# Security

The OwnTracks Recorder is a simple program which has basically a single task which is to take location publishes from OwnTracks clients, perform optional geo-lookups, and store the data.

The program was initially an MQTT client only so protected by an MQTT broker. This broker (server) is the component which provides authentication, authorization, transport level security (TLS/SSL), and the Recorder "simply" subscribes to this broker at a particular MQTT topic branch and receives what the broker gives it.

In MQTT mode, the Recorder has no open incoming TCP ports -- it connects out to an MQTT broker (either TCP/1883 or TCP/8883, configurable) and optionally to a geo-location service (TCP/443, likewise configurable).


## file system

Data is stored by the Recorder in either plain files or in an LMDB database, the latter for geo-lookups and sundry other data (see [STORE](store.md)]. Some of this data includes possible payload encryption keys (see below) so the database ought to be well protected. Both the Recorder (`ot-recorder`), and its `ocat` utility are installed SUID by default; it is assumed that these utilities will be used on single-user systems fronted by proxies. If this is undesirable, it is possible to install these programs with different permissions.

Any user which has access to the files into which the OwnTracks Recorder stores data can obviously read that data.

## HTTP

Roughly in 2015 we decided to offer HTTP for those people who weren't willing to stand up an MQTT broker, imagining that HTTP would be easier for them. Our apps got HTTP support as did the Recorder. But that meant that the Recorder needed incoming HTTP (TCP/80, configurable) for the apps to connect to and publish their location payloads.

This was implemented, and we explicitly did not implement authorization, authentication, or transport level security, making users aware that a HTTP proxy (e.g. nginx or Apache) ought to be used for implementing those capabilities.

Implementing HTTP caused a whole slew of additional issues we needed to resolve, e.g. support for Friends, special support for CARDs, etc. most of which we were able to address, even if they occasionally appear to be kludges, which in fact they frequently are.

Very specifically we need to warn Recorder users using HTTP:
- any client permitted to access the `/pub` endpoint can publish data to the Recorder
- any client permitted to access the `/api` endpoint can access any user's data, and even destroy it, depending on whether or not the Recorder was built with the `WITH_KILL` flag. (Since July 2026, our packages are built with this setting set to `no`, i.e. disabled, and the feature has been removed entirely in November 2026.)
- any client permitted to access the `/ws` endpoint can access users' LAST locations

## payload encryption

In addition to using TLS connections, either for MQTT or HTTP, you may wish to enable payload encryption within the app. We have [documented this in the Booklet](https://owntracks.org/booklet/features/encrypt/).

Please pay attention to the Notes section of that page, as payload encryption might well be counterproductive for your use-case. Also note, that encrypted payloads received by the Recorder are decrypted if possible, and stored in clear in the corresponding files on the file system.



## further reading

Please also be aware of [our security recommendations for the apps](https://owntracks.org/booklet/features/security/), particularly the changes done here beginning in July 2026.
