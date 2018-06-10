all: radio
install: all

OS=$(shell uname -s)
CXXFLAGS=-g -Wall -I../include
ifeq ($(OS),Linux)
CXXFLAGS += -Wno-unused-but-set-variable
endif

INCLS=dqueue.h ../include/xgml.h

OBJS=radio.o

$(OBJS): $(INCLS)

radio.o: radio.cc $(INCLS)

radio: $(OBJS) ../lib/librpc.a ../lib/libext.a ../lib/libstream.a ../lib/librst.a ../lib/libcore.a
	c++ -g -o radio $(OBJS) ../lib/libext.a ../lib/libstream.a ../lib/librst.a ../lib/librpc.a ../lib/libcore.a

clean:
	rm -f *.o radio *.dSYM
