CC=gcc
CFLAGS=-Wall -Wextra -Iincludes
LDLIBS=-lm
VPATH=src

all: rserver

rserver: rserver.o optparser.o helper.o

clean:
	rm -f *~ *.o rserver
