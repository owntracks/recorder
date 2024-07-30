# Design decisions

We took a number of decisions when designing the Recorder and its utilities:

* Flat files. The filesystem is the database. Period. That's were everything is stored. It makes incremental backups, purging old data, manipulation via the Unix toolset easy. (Admittedly, for fast geo-lookups we employ LMDB as a cache, but the final word is in the filesystem.) We considered all manner of databases and decided to keep this as simple and lightweight as possible. You can however have the Recorder send data to a database of your choosing, in addition to the file system it uses, by utilizing our embedded Lua hook.
* We wanted to store received data in the format it's published in. As this format is JSON, we store this raw payload in the `.rec` files. If we add an attribute to the JSON published by our apps, you have it right there. There's one slight exception: the monthly logs (the `.rec` files) have a leading timestamp and a relative topic; see below.
* File names are lower case. A user called `JaNe` with a device named `myPHONe` will be found in a file named `jane/myphone`.
* All times are UTC (a.k.a. Zulu or GMT). We got sick and tired of converting stuff back and forth. It is up to the consumer of the data to convert to local time if need be.
* The Recorder does not really provide authentication or authorization. Well, it does a bit. Think about this before making it available on a publicly-accessible IP address. Or rather: don't think about it; just don't do it. You can of course place a HTTP proxy in front of the Recorder to control access to it. Or use views (see below).
* `ocat`, the `cat` program for the Recorder, uses the same back-end which is used by the API though it accesses it directly (i.e. without resorting to HTTP).
* The Recorder supports 3-level MQTT topics only, in the typical OwnTracks format: `"owntracks/<username>/<devicename>"`, optionally with a leading slash. (The first part of the topic need not be "owntracks".) Publishes via HTTP POST construct a fictitious topic internally using the provided user (`u`) and device (`d`) parameters.
