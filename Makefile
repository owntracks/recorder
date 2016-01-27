include config.mk

CFLAGS	=-Wall -Werror $(MOSQUITTO_INC)
LIBS	= $(MORELIBS) $(MOSQUITTO_LIB) -lmosquitto -lm
LIBS 	+= -lcurl

TARGETS=
OTR_OBJS = json.o \
	   geo.o \
	   geohash.o \
	   mkpath.o \
	   base64.o \
	   misc.o \
	   util.o \
	   storage.o \
	   listsort.o

CFLAGS += -DGHASHPREC=$(GHASHPREC)

ifeq ($(WITH_PING),yes)
	CFLAGS += -DWITH_PING=1
endif

ifeq ($(WITH_LUA),yes)
	CFLAGS += -DWITH_LUA=1 $(LUA_CFLAGS)
	LIBS   += $(LUA_LIBS)
	OTR_OBJS += hooks.o
endif

ifeq ($(WITH_ENCRYPT),yes)
	CFLAGS += -DWITH_ENCRYPT=1 $(SODIUM_CFLAGS)
	LIBS   += $(SODIUM_LIBS)
endif

ifeq ($(WITH_KILL),yes)
	CFLAGS += -DWITH_KILL=1
endif

ifeq ($(WITH_RONLY),yes)
	CFLAGS += -DWITH_RONLY=1
	WITH_LMDB = yes
endif

ifeq ($(WITH_LMDB),yes)
	CFLAGS += -DWITH_LMDB=1 -Imdb/
	OTR_OBJS += gcache.o
	LIBS += mdb/liblmdb.a -lpthread
	TARGETS += mdb/liblmdb.a
endif

ifeq ($(WITH_HTTP),yes)
	CFLAGS += -DWITH_HTTP=1
	OTR_OBJS += mongoose.o http.o
endif

ifeq ($(WITH_GREENWICH),yes)
	CFLAGS += -DWITH_GREENWICH=1
endif

ifeq ($(JSON_INDENT),yes)
	CFLAGS += -DJSON_INDENT="\" \""
else
	CFLAGS += -DJSON_INDENT=NULL
endif

CFLAGS += -DSTORAGEDEFAULT=\"$(STORAGEDEFAULT)\" -DDOCROOT=\"$(DOCROOT)\"



TARGETS += ot-recorder ocat

all: $(TARGETS)

ot-recorder: recorder.o $(OTR_OBJS)
	$(CC) $(CFLAGS) -o ot-recorder recorder.o $(OTR_OBJS) $(LIBS)
	if test -r codesign.sh; then /bin/sh codesign.sh; fi

ocat: ocat.o $(OTR_OBJS)
	$(CC) $(CFLAGS) -o ocat ocat.o $(OTR_OBJS) $(LIBS)

$(OTR_OBJS): config.mk Makefile

recorder.o: recorder.c storage.h util.h Makefile geo.h udata.h json.h http.h gcache.h config.mk hooks.h base64.h
geo.o: geo.h geo.c udata.h
geohash.o: geohash.h geohash.c udata.h
base64.o: base64.h base64.c
gcache.o: gcache.c gcache.h json.h
misc.o: misc.c misc.h udata.h
http.o: http.c mongoose.h util.h http.h storage.h version.h
util.o: util.c util.h
mongoose.o: mongoose.c mongoose.h
ocat.o: ocat.c storage.h util.h version.h config.mk Makefile
storage.o: storage.c storage.h util.h gcache.h listsort.h
hooks.o: hooks.c udata.h hooks.h util.h version.h gcache.h
listsort.o: listsort.c listsort.h


clean:
	rm -f *.o
clobber: clean
	rm -f ot-recorder ocat

mdb/liblmdb.a:
	(cd mdb && make)

install: ot-recorder ocat
	mkdir -p $(DESTDIR)$(INSTALLDIR)/bin
	mkdir -p $(DESTDIR)$(INSTALLDIR)/sbin
	mkdir -p $(DESTDIR)$(DOCROOT)
	mkdir -p $(DESTDIR)$(STORAGEDEFAULT)/last
	cp -R docroot/* $(DESTDIR)$(DOCROOT)/
	install -m 0755 ot-recorder $(DESTDIR)$(INSTALLDIR)/sbin
	install -m 0755 ocat $(DESTDIR)$(INSTALLDIR)/bin
ifndef DESTDIR
        $(INSTALLDIR)/sbin/ot-recorder --initialize
endif
	# mkdir -p $(DESTDIR)/etc/systemd/system/
	# install --mode 0644 etc/ot-recorder.service $(DESTDIR)/etc/systemd/system/ot-recorder.service
