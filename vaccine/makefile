all: zipdecode ritestores ritecheck walcheck tsp

install: all
	echo Nothing to install for vaccine

clean:
	rm -f *.o *.a zipdecode ritestores ritecheck walcheck tsp

OS=$(shell uname -s)

ifneq ($(OS),Linux)
OSXVERSION=-mmacosx-version-min=10.10
else
OSXVERSION=
endif

CXXFLAGS=-g -Wall -I../include -I/usr/local/ssl/include $(OSXVERSION)
CFLAGS=-g -Wall -I../include -I/usr/local/ssl/include $(OSXVERSION)
ifeq ($(OS),Linux)
CXXFLAGS += -Wno-sign-compare -Wno-unused-but-set-variable 
endif

INCLS=../include/*.h distutils.h

zipdecode.o: zipdecode.cc $(INCLS)

zipdecode: zipdecode.o ../lib/libcore.a ../lib/librpc.a ../lib/libext.a
	g++ -g -o zipdecode zipdecode.o ../lib/libcore.a ../lib/librpc.a ../lib/libext.a -lpthread

ritestores.o: ritestores.cc $(INCLS)

ritestores: ritestores.o ../lib/libcore.a ../lib/librpc.a ../lib/libext.a ../lib/librst.a
	g++ -g -o ritestores ritestores.o ../lib/librst.a ../lib/libcore.a ../lib/librpc.a ../lib/libext.a -lssl -lcrypto -lpthread

ritecheck.o: ritecheck.cc $(INCLS)

distutils.o: distutils.cc $(INCLS)

ritecheck: ritecheck.o distutils.o ../lib/libcore.a ../lib/librpc.a ../lib/libext.a ../lib/librst.a
	g++ -g -o ritecheck ritecheck.o distutils.o ../lib/librst.a ../lib/libcore.a ../lib/librpc.a ../lib/libext.a -lssl -lcrypto -lpthread

walcheck.o: walcheck.cc $(INCLS)

walcheck: walcheck.o distutils.o ../lib/libcore.a ../lib/librpc.a ../lib/libext.a ../lib/librst.a
	g++ -g -o walcheck walcheck.o ../lib/librst.a ../lib/libcore.a ../lib/librpc.a ../lib/libext.a -lssl -lcrypto -lpthread

tsp.o: tsp.cc $(INCLS)

tsp: tsp.o ../lib/libcore.a ../lib/libext.a
	g++ -g -o tsp tsp.o ../lib/libcore.a ../lib/libext.a -lcurl
