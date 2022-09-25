include config.mk

CFLAGS	+=-Wall -Werror -DNS_ENABLE_IPV6
LIBS	= $(MORELIBS) -lm
LIBS 	+= -lcurl -lconfig

TARGETS=
OTR_OBJS = json.o \
	   gcache.o \
	   geo.o \
	   geohash.o \
	   mkpath.o \
	   base64.o \
	   misc.o \
	   util.o \
	   storage.o \
	   fences.o \
	   listsort.o
OTR_EXTRA_OBJS =

CFLAGS += -DGHASHPREC=$(GHASHPREC)

LIBS += -llmdb
LIBS += -lpthread

define CPP_CONDITION
printf '#if $(1) \n
true \n
#else \n
#error false \n
#endif' | $(CPP) -P - >/dev/null 2>&1 && echo yes
endef

ifeq ($(WITH_MQTT),yes)
	CFLAGS += -DWITH_MQTT=1
	CFLAGS += $(MOSQUITTO_CFLAGS)
	LIBS += $(MOSQUITTO_LIBS) -lm
endif

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

ifeq ($(WITH_HTTP),yes)
	CFLAGS += -DWITH_HTTP=1
	OTR_EXTRA_OBJS += mongoose.o http.o
endif

ifeq ($(WITH_TOURS),yes)
	CFLAGS += -DWITH_TOURS
	OTR_EXTRA_OBJS +=

	# Debian requires uuid-dev
	# RHEL/CentOS needs libuuid-devel
	ifeq ($(shell $(call CPP_CONDITION,__linux__)),yes)
		LIBS += -luuid
	endif
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
CFLAGS += -DCONFIGFILE=\"$(CONFIGFILE)\"
CFLAGS += -DGEOCODE_TIMEOUT=$(GEOCODE_TIMEOUT)

TARGETS += ot-recorder ocat

GIT_VERSION := $(shell git describe --long --abbrev=10 --dirty --tags 2>/dev/null || echo "tarball")
CFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"

PKG_CONFIG ?= pkg-config

all: $(TARGETS)

ot-recorder: recorder.o $(OTR_OBJS) $(OTR_EXTRA_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o ot-recorder recorder.o $(OTR_OBJS) $(OTR_EXTRA_OBJS) $(LIBS)
	if test -r codesign.sh; then /bin/sh codesign.sh; fi

ocat: ocat.o $(OTR_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o ocat ocat.o $(OTR_OBJS) $(LIBS)

$(OTR_OBJS): config.mk Makefile

recorder.o: recorder.c storage.h util.h Makefile geo.h udata.h json.h http.h gcache.h config.mk hooks.h base64.h recorder.h version.h fences.h
geo.o: geo.h geo.c udata.h
geohash.o: geohash.h geohash.c udata.h
base64.o: base64.h base64.c
	$(CC) $(CFLAGS) -Wno-unused-result -Wno-uninitialized -c base64.c
gcache.o: gcache.c gcache.h json.h
misc.o: misc.c misc.h udata.h
http.o: http.c mongoose.h util.h http.h storage.h version.h hooks.h
util.o: util.c util.h
mongoose.o: mongoose.c mongoose.h
ocat.o: ocat.c storage.h util.h version.h config.mk Makefile
storage.o: storage.c storage.h util.h gcache.h listsort.h
hooks.o: hooks.c udata.h hooks.h util.h version.h gcache.h
listsort.o: listsort.c listsort.h
fences.o: fences.c fences.h util.h json.h udata.h gcache.h hooks.h


clean:
	rm -f *.o
clobber: clean
	rm -f ot-recorder ocat

install: ot-recorder ocat
	mkdir -p $(DESTDIR)$(INSTALLDIR)/bin
	mkdir -p $(DESTDIR)$(INSTALLDIR)/sbin
	mkdir -p $(DESTDIR)$(DOCROOT)
	mkdir -p $(DESTDIR)$(STORAGEDEFAULT)
	cp -R docroot/* $(DESTDIR)$(DOCROOT)/
	install -m 0755 ot-recorder $(DESTDIR)$(INSTALLDIR)/sbin
	install -m 0755 ocat $(DESTDIR)$(INSTALLDIR)/bin
	mkdir -p `dirname $(DESTDIR)/$(CONFIGFILE)`
	test -r $(DESTDIR)/$(CONFIGFILE) || install -m 640 etc/ot-recorder.default $(DESTDIR)/$(CONFIGFILE)
ifndef DESTDIR
	$(INSTALLDIR)/sbin/ot-recorder --initialize
endif
	# mkdir -p $(DESTDIR)/etc/systemd/system/
	# install --mode 0644 etc/ot-recorder.service $(DESTDIR)/etc/systemd/system/ot-recorder.service
