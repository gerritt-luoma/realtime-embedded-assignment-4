INCLUDE_DIRS = 
LIB_DIRS = 
CC=gcc

CDEFS=
CFLAGS= -O3 -g -Wcpp $(INCLUDE_DIRS) $(CDEFS)
LIBS= -lrt -lpthread

HFILES= 
CFILES= capture.c capture-srt.c

SRCS= ${HFILES} ${CFILES}
OBJS= ${CFILES:.c=.o}

all:	capture capture-srt

clean:
	-rm -f *.o *.d capture capture-srt
	-rm -f frames/*

distclean:
	-rm -f *.o *.d

%: %.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $< $(LIBS)

depend:

.c.o:
	$(CC) $(CFLAGS) -c $<
