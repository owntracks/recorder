include config.mk

LIBS = -L/Users/jpm/Auto/pubgit/MQTT/mosquitto/org.eclipse.mosquitto.git/lib
LIBS += -lcurl -lmosquitto
CFLAGS=-Wall -Werror

OTR_OBJS = json.o \
	   geo.o \
	   geohash.o \
	   mkpath.o \
	   file.o \
	   safewrite.o \
	   base64.o \
	   ghash.o \
	   misc.o \
	   util.o \
	   storage.o

ifneq ($(HAVE_REDIS),no)
	CFLAGS += -DHAVE_REDIS=1
	LIBS += -lhiredis
endif

ifeq ($(HAVE_HTTP),yes)
	CFLAGS += -DHAVE_HTTP=1
	OTR_OBJS += mongoose.o http.o
	LIBS += -lssl
endif


all: ot-recorder ocat ghashfind

ot-recorder: ot-recorder.c $(OTR_OBJS)
	$(CC) $(CFLAGS) ot-recorder.c -o ot-recorder $(OTR_OBJS) $(LIBS)

ot-recorder.o: ot-recorder.c storage.h
geo.o: geo.h geo.c udata.h Makefile config.mk config.h
geohash.o: geohash.h geohash.c udata.h Makefile config.mk
file.o: file.h file.c config.h misc.h Makefile config.mk
base64.o: base64.h base64.c
ghash.o: ghash.h ghash.c config.h udata.h misc.h Makefile config.mk
safewrite.o: safewrite.h safewrite.c
jget.o: jget.c jget.h json.h Makefile config.mk
misc.o: misc.c misc.h udata.h Makefile config.mk
http.o: http.c mongoose.h util.h http.h storage.h
util.o: util.c util.h Makefile config.mk

ocat: ocat.o storage.o json.o geohash.o ghash.o mkpath.o util.o
	$(CC) $(CFLAGS) -o ocat ocat.o storage.o json.o geohash.o ghash.o mkpath.o util.o $(LIBS)

ocat.o: ocat.c storage.h
storage.o: storage.c storage.h config.h util.h

ghashfind: ghashfind.o util.o json.o
	$(CC) $(CFLAGS) -o ghashfind ghashfind.o util.o json.o
ghashfind.o: ghashfind.c util.h
mongoose.o: mongoose.c mongoose.h

clean:
	rm -f *.o
clobber: clean
	rm -f ot-recorder ocat
