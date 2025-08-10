all: libvoter.a vtest

install: all
	cp *.a ../lib
	cp *.h ../include

clean:
	rm -f *.o *.a vtest

OS=$(shell uname -s)

ifneq ($(OS),Linux)
OSXVERSION=-mmacosx-version-min=12.0
else
OSXVERSION=
endif

CXXFLAGS=-g -Wall -I../include -I/usr/local/ssl/include $(OSXVERSION) -std=c++17
CFLAGS=-g -Wall -I../include -I/usr/local/ssl/include $(OSXVERSION)
ifeq ($(OS),Linux)
CXXFLAGS += -Wno-sign-compare -Wno-unused-but-set-variable
endif

INCLS=voter.h votersdr.h ../include/rpc.h ../include/osp.h

vtest.o: ${INCLS} vtest.cc

voter.o: ${INCLS} voter.cc

libvoter.a: voter.o
	ar cru libvoter.a voter.o
	ranlib libvoter.a

vtest: vtest.o libvoter.a ../lib/librpc.a ../lib/libext.a ../lib/libcore.a
	c++ $(OSXVERSION) -o vtest vtest.o libvoter.a ../lib/librpc.a ../lib/libext.a ../lib/libcore.a -lpthread
