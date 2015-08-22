include config.mk

LIBS = -L/Users/jpm/Auto/pubgit/MQTT/mosquitto/org.eclipse.mosquitto.git/lib
LIBS += -lcurl -lmosquitto
CFLAGS=-Wall -Werror -g

ifneq ($(HAVE_REDIS),no)
	CFLAGS += -DHAVE_REDIS=1
	LIBS += -lhiredis
endif


all: ot-recorder ocat

ot-recorder: ot-recorder.c json.o utarray.h utstring.h geo.o geohash.o mkpath.o file.o safewrite.o base64.o ghash.o config.h udata.h misc.o util.o
	$(CC) $(CFLAGS) ot-recorder.c -o ot-recorder json.o geo.o geohash.o mkpath.o file.o safewrite.o base64.o ghash.o misc.o util.o $(LIBS)

geo.o: geo.h geo.c udata.h Makefile config.mk config.h
geohash.o: geohash.h geohash.c udata.h Makefile config.mk
file.o: file.h file.c config.h misc.h Makefile config.mk
base64.o: base64.h base64.c
ghash.o: ghash.h ghash.c config.h udata.h misc.h Makefile config.mk
safewrite.o: safewrite.h safewrite.c
jget.o: jget.c jget.h json.h Makefile config.mk
misc.o: misc.c misc.h udata.h Makefile config.mk
util.o: util.c util.h Makefile config.mk

ocat: ocat.o storage.o json.o geohash.o ghash.o mkpath.o util.o
	$(CC) $(CFLAGS) -o ocat ocat.o storage.o json.o geohash.o ghash.o mkpath.o util.o $(LIBS)

ocat.o: ocat.c storage.h
storage.o: storage.c storage.h config.h

clean:
	rm -f *.o
clobber: clean
	rm -f ot-recorder ocat
