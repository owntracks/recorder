include config.mk

LIBS = -L/Users/jpm/Auto/pubgit/MQTT/mosquitto/org.eclipse.mosquitto.git/lib
LIBS += -lcurl -lmosquitto
CFLAGS=-Wall -Werror

TARGETS=
OTR_OBJS = json.o \
	   geo.o \
	   geohash.o \
	   mkpath.o \
	   file.o \
	   safewrite.o \
	   base64.o \
	   misc.o \
	   util.o \
	   storage.o

ifeq ($(HAVE_LMDB),yes)
	CFLAGS += -DHAVE_LMDB=1 -Imdb/
	OTR_OBJS += gcache.o
	LIBS += mdb/liblmdb.a -lpthread
	TARGETS += mdb/liblmdb.a gcache-dump ghash2lmdb
endif

ifeq ($(HAVE_HTTP),yes)
	CFLAGS += -DHAVE_HTTP=1
	OTR_OBJS += mongoose.o http.o
	LIBS += -lssl
endif


TARGETS += ot-recorder ocat ghashfind
all: $(TARGETS)

ot-recorder: ot-recorder.o $(OTR_OBJS)
	$(CC) $(CFLAGS) ot-recorder.o -o ot-recorder $(OTR_OBJS) $(LIBS)

ot-recorder.o: ot-recorder.c storage.h util.h Makefile geo.h udata.h config.h json.h http.h
geo.o: geo.h geo.c udata.h Makefile config.mk config.h
geohash.o: geohash.h geohash.c udata.h Makefile config.mk
file.o: file.h file.c config.h misc.h Makefile config.mk
base64.o: base64.h base64.c
gcache.o: gcache.c gcache.h json.h
safewrite.o: safewrite.h safewrite.c
jget.o: jget.c jget.h json.h Makefile config.mk
misc.o: misc.c misc.h udata.h Makefile config.mk
http.o: http.c mongoose.h util.h http.h storage.h
util.o: util.c util.h Makefile config.mk

ocat: ocat.o storage.o json.o geohash.o mkpath.o util.o gcache.o
	$(CC) $(CFLAGS) -o ocat ocat.o storage.o json.o geohash.o mkpath.o util.o gcache.o $(LIBS)

ocat.o: ocat.c storage.h
storage.o: storage.c storage.h config.h util.h

ghashfind: ghashfind.o util.o json.o
	$(CC) $(CFLAGS) -o ghashfind ghashfind.o util.o json.o
ghashfind.o: ghashfind.c util.h
mongoose.o: mongoose.c mongoose.h

clean:
	rm -f *.o
clobber: clean
	rm -f ot-recorder ocat ghashfind gcache-dump

mdb/liblmdb.a:
	(cd mdb && make)

gcache-dump: gcache-dump.o
	$(CC) $(CFLAGS) -o gcache-dump gcache-dump.c $(LIBS)
ghash2lmdb: ghash2lmdb.o util.o json.o gcache.o 
	$(CC) $(CFLAGS) -o ghash2lmdb ghash2lmdb.c util.o json.o gcache.o $(LIBS)
