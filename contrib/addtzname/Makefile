
CFLAGS=-I../..	\
	-DTZDATADB="\"../../sundry/zonedetect/ZoneDetect/out/timezone16.bin\""

OBJS=../../json.o ../../zonedetect.o ../../geohash.o

addtzname: addtzname.c $(OBJS)
	$(CC) $(CFLAGS) -o addtzname addtzname.c $(OBJS)
