all: vantest yftest libvan.a

install: all *.h
	cp -p *.h ../include/.
	cp -p *.a ../lib/.

clean:
	rm -f *.o *.a yftest vantest

OS=$(shell uname -s)

ifneq ($(OS),Linux)
OSXVERSION=-mmacosx-version-min=12.0
CRYPTO_LFLAGS=/usr/local/lib/libssl.a /usr/local/lib/libcrypto.a 
else
OSXVERSION=
CRYPTO_LFLAGS=-lssl -lcrypto 
endif

CXXFLAGS=-g -Wall -I../include -I/usr/local/ssl/include $(OSXVERSION)
CFLAGS=-g -Wall -I../include -I/usr/local/ssl/include $(OSXVERSION)
ifeq ($(OS),Linux)
CXXFLAGS += -Wno-sign-compare -Wno-unused-but-set-variable 
else
CXXFLAGS += -Wno-c++11-extensions -std=c++14
endif

LIBS=libvan.a
INCLS=vanofx.h utils.h yfdriver.h

vanofx.o: vanofx.cc $(INCLS)

utils.o: utils.cc $(INCLS)

yfdriver.o: yfdriver.cc $(INCLS)

libvan.a: vanofx.o utils.o yfdriver.o
	ar cru libvan.a vanofx.o utils.o yfdriver.o

vantest.o: vantest.cc $(INCLS)

vantest: vantest.o $(LIBS)
	c++ ${OSXVERSION} -o vantest vantest.o $(LIBS)

yftest.o: yftest.cc $(INCLS)

yftest: yftest.o $(LIBS)
	c++ ${OSXVERSION} -o yftest yftest.o $(LIBS)
