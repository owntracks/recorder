
CFLAGS=-I../..	\
	-DTZDATADB="\"../tzdatadb/timezone16.bin\""

OBJS=../../json.o ../../zonedetect.o ../../geohash.o

addtzname: addtzname.c $(OBJS) Makefile
	$(CC) $(CFLAGS) -o addtzname addtzname.c $(OBJS) -lm
