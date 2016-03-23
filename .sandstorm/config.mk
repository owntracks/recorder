# Select features you want. Default is fine for most installations

# Uncomment the following for FreeBSD; this assumes:
#
#       cd /usr/ports/ftp/curl; make config; make install clean
#       cd /usr/ports/devel/libconfig; make config; make install clean
#
# CC      = clang
# MDBFLAGS = -DMDB_DSYNC=O_SYNC -Dfdatasync=fsync
# CFLAGS += $(MDBFLAGS) -I/usr/local/include
# MORELIBS += -L /usr/local/lib
# -- end FreeBSD

INSTALLDIR = /usr/local

# Do you want support for MQTT?
WITH_MQTT ?= no

# Do you want recorder's built-in HTTP REST API?
WITH_HTTP ?= yes

# Do you have Lua libraries installed and want the Lua hook integration?
WITH_LUA ?= no

# Do you want support for the `pingping' monitoring feature?
WITH_PING ?= yes

# Do you want support for removing data via the API? (Dangerous)
WITH_KILL ?= no

# Do you want support for payload encryption with libsodium?
# This requires WITH_LMDB to be configured.
WITH_ENCRYPT ?= NO

# Do you require support for OwnTracks Greenwich firmware?
WITH_GREENWICH ?= no

# Where should the recorder store its data? This directory must
# exist and be writeable by recorder (and readable by ocat)
STORAGEDEFAULT = /var/store

# Where should the recorder find its document root (HTTP)?
DOCROOT = /opt/app/docroot

# Define the precision for reverse-geo lookups. The higher
# the number, the more granular reverse-geo will be:
#
# 1	=> 5,009.4km x 4,992.6km
# 2	=> 1,252.3km x 624.1km
# 3	=> 156.5km x 156km
# 4	=> 39.1km x 19.5km
# 5	=> 4.9km x 4.9km
# 6	=> 1.2km x 609.4m
# 7	=> 152.9m x 152.4m
# 8	=> 38.2m x 19m
# 9	=> 4.8m x 4.8m
# 10	=> 1.2m x 59.5cm

GHASHPREC = 7

# Should the JSON emitted by recorder be indented? (i.e. beautified)
# yes or no
JSON_INDENT ?= no

# Location of optional default configuration file
#CONFIGFILE = /etc/defaults/ot-recorder

# Optionally specify the path to the Mosquitto libs, include here
MOSQUITTO_INC = -I/usr/include
MOSQUITTO_LIB = -L/usr/lib
MORELIBS = # -lssl

# If WITH_LUA is configured, specify compilation and linkage flags
# for Lua either manually or using pkg-config. This may require tweaking,
# and in particular could require you to add the lua+version (e.g lua-5.2)
# to both pkg-config invocations

LUA_CFLAGS = `pkg-config --cflags lua`
LUA_LIBS   = `pkg-config --libs lua`

SODIUM_CFLAGS = `pkg-config --cflags libsodium`
SODIUM_LIBS   = `pkg-config --libs libsodium`
