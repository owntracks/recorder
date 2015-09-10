include config.mk

CFLAGS	=-Wall -Werror $(MOSQUITTO_INC)
LIBS	= $(MOSQUITTO_LIB) -lmosquitto
LIBS 	+= -lcurl

TARGETS=
OTR_OBJS = json.o \
	   geo.o \
	   geohash.o \
	   mkpath.o \
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

CFLAGS += -DSTORAGEDEFAULT=\"$(STORAGEDEFAULT)\"



TARGETS += ot-recorder ocat ghashfind

all: $(TARGETS)

ot-recorder: ot-recorder.o $(OTR_OBJS)
	$(CC) $(CFLAGS) -o ot-recorder ot-recorder.o $(OTR_OBJS) $(LIBS)

ocat: ocat.o $(OTR_OBJS)
	$(CC) $(CFLAGS) -o ocat ocat.o $(OTR_OBJS) $(LIBS)

ghashfind: ghashfind.o $(OTR_OBJS)
	$(CC) $(CFLAGS) -o ghashfind ghashfind.o $(OTR_OBJS) $(LIBS)

gcache-dump: gcache-dump.o
	$(CC) $(CFLAGS) -o gcache-dump gcache-dump.o $(LIBS)

ghash2lmdb: ghash2lmdb.o $(OTR_OBJS)
	$(CC) $(CFLAGS) -o ghash2lmdb ghash2lmdb.o $(OTR_OBJS) $(LIBS)


ot-recorder.o: ot-recorder.c storage.h util.h Makefile geo.h udata.h config.h json.h http.h gcache.h
geo.o: geo.h geo.c udata.h Makefile config.mk config.h
geohash.o: geohash.h geohash.c udata.h Makefile config.mk
base64.o: base64.h base64.c
gcache.o: gcache.c gcache.h json.h
jget.o: jget.c jget.h json.h Makefile config.mk
misc.o: misc.c misc.h udata.h Makefile config.mk
http.o: http.c mongoose.h util.h http.h storage.h
util.o: util.c util.h Makefile config.mk
ghashfind.o: ghashfind.c util.h
mongoose.o: mongoose.c mongoose.h
ocat.o: ocat.c storage.h util.h config.mk version.h
storage.o: storage.c storage.h config.h util.h gcache.h
ghash2lmdb.o: ghash2lmdb.c gcache.h


clean:
	rm -f *.o
clobber: clean
	rm -f ot-recorder ocat ghashfind gcache-dump

mdb/liblmdb.a:
	(cd mdb && make)

