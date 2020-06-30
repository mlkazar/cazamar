
all: libdisp.a librcall.a disptest sltest rbuftest

install: all

clean:
	rm -f *.o *.a disptest sltest rbuftest
	rm -rf *.dSYM

OS=$(shell uname -s)

ifneq ($(OS),Linux)
OSXVERSION=-mmacosx-version-min=10.10
else
OSXVERSION=
endif

LIBS=../lib/libcore.a ../lib/librpc.a

CXXFLAGS=-g -Wall -I../include $(WARNFLAGS) $(OSXVERSION)
ifeq ($(OS),Linux)
CXXFLAGS += -Wno-unused-but-set-variable
endif

INCLS=ezcall.h rsfwds.h disp.h lock.h sock.h socklocal.h rbuf.h rbufstr.h ../include/*.h

disp.o: disp.cc ${INCLS}

lock.o: lock.cc ${INCLS}

libdisp.a: osptimer.o lock.o disp.o
	rm -f libdisp.a
	ar -cr libdisp.a osptimer.o lock.o disp.o
	ranlib libdisp.a

disptest.o: disptest.cc ${INCLS}

disptest: disptest.o libdisp.a $(LIBS)
	g++ -g -o disptest disptest.o libdisp.a $(LIBS)

sltest.o: sltest.cc ${INCLS}

socklocal.o: socklocal.cc $(INCLS)

sltest: sltest.o socklocal.o libdisp.a librcall.a $(LIBS)
	g++ -g -o sltest sltest.o socklocal.o libdisp.a librcall.a $(LIBS)

rbufstr.o: rbufstr.cc $(INCLS)

rbuf.o: rbuf.cc $(INCLS)

ezcall.o: ezcall.cc $(INCLS)

librcall.a: rbufstr.o rbuf.o ezcall.o
	rm -f librcall.a
	ar -cr librcall.a rbufstr.o rbuf.o ezcall.o
	ranlib librcall.a

rbuftest.o: rbuftest.cc librcall.a $(INCLS)

rbuftest: rbuftest.o librcall.a
	g++ -g -o rbuftest rbuftest.o librcall.a

install: all
	cp -p libdisp.a librcall.a ../lib
