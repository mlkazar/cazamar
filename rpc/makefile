all: librpc.a libext.a libcore.a rpctest jsontest xgmltest

install: all
	cp *.a ../lib
	cp *.h ../include

clean:
	rm -f *.o *.a rpctest jsontest xgmltest

#OSXVERSION=-mmacosx-version-min=10.7
OSXVERSION=

OS=$(shell uname -s)
CXXFLAGS=-g -Wall $(WARNFLAGS) $(OSXVERSION)
ifeq ($(OS),Linux)
CXXFLAGS += -Wno-unused-but-set-variable
endif


INCLS=cthread.h dqueue.h json.h osp.h ospmbuf.h osptypes.h sdr.h restcall.h rpc.h xgml.h 

rpc.o: rpc.cc $(INCLS)

cthread.o: cthread.cc $(INCLS)

osp.o: osp.cc $(INCLS)

sdr.o: sdr.cc $(INCLS)

xgml.o: xgml.cc $(INCLS)

json.o: json.cc $(INCLS)

jsontest.o: jsontest.cc $(INCLS)

restcall.o: restcall.cc $(INCLS)

rpctest.o: rpctest.cc $(INCLS)

libext.a: sdr.o xgml.o json.o
	ar cru libext.a sdr.o xgml.o json.o
	ranlib libext.a

libcore.a: osp.o cthread.o
	ar cru libcore.a osp.o cthread.o
	ranlib libcore.a

librpc.a: rpc.o restcall.o
	ar cru librpc.a rpc.o restcall.o
	ranlib librpc.a

rpctest: rpctest.o librpc.a libext.a libcore.a
	c++ $(OSXVERSION) -o rpctest rpctest.o librpc.a libext.a libcore.a  -lpthread

jsontest: jsontest.o libext.a
	c++ $(OSXVERSION) -o jsontest jsontest.o libext.a

xgmltest: xgmltest.o libext.a
	c++ $(OSXVERSION) -o xgmltest xgmltest.o libext.a
