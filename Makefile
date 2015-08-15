CFLAGS=-Wall -Werror -g

all: ot-recorder #### ot-reader

ot-reader: ot-reader.c json.o utstring.h ghash.o mkpath.o jget.o
	cc $(CFLAGS) ot-reader.c -o ot-reader json.o ghash.o mkpath.o jget.o -lhiredis

ot-recorder: ot-recorder.c json.o utarray.h utstring.h geo.o geohash.o mkpath.o file.o safewrite.o base64.o ghash.o config.h udata.h misc.o
	cc $(CFLAGS) ot-recorder.c -o ot-recorder json.o geo.o geohash.o mkpath.o file.o safewrite.o base64.o ghash.o misc.o -lmosquitto -lcurl -lhiredis

geo.o: geo.h geo.c udata.h
geohash.o: geohash.h geohash.c udata.h
file.o: file.h file.c config.h
base64.o: base64.h base64.c
ghash.o: ghash.h ghash.c config.h udata.h
safewrite.o: safewrite.h safewrite.c
jget.o: jget.c jget.h json.h
misc.o: misc.c misc.h udata.h
