all: librpc.a libext.a libcore.a rpctest jsontest xgmltest cdisptest timertest

install: all
	cp *.a ../lib
	cp *.h ../include

clean:
	rm -f *.o *.a rpctest jsontest xgmltest cdisptest timertest

OS=$(shell uname -s)

ifneq ($(OS),Linux)
OSXVERSION=-mmacosx-version-min=10.10
else
OSXVERSION=
endif

CXXFLAGS=-g -Wall $(WARNFLAGS) $(OSXVERSION)
ifeq ($(OS),Linux)
CXXFLAGS += -Wno-unused-but-set-variable
endif


INCLS=cdisp.h cthread.h dqueue.h json.h osp.h ospmbuf.h osptypes.h osptimer.h sdr.h restcall.h rpc.h xgml.h 

rpc.o: rpc.cc $(INCLS)

cdisp.o: cdisp.cc $(INCLS)

cdisptest.o: cdisptest.cc $(INCLS)

timertest.o: timertest.cc $(INCLS)

cthread.o: cthread.cc $(INCLS)

osp.o: osp.cc $(INCLS)

osptimer.o: osptimer.cc $(INCLS)

sdr.o: sdr.cc $(INCLS)

xgml.o: xgml.cc $(INCLS)

json.o: json.cc $(INCLS)

jsontest.o: jsontest.cc $(INCLS)

restcall.o: restcall.cc $(INCLS)

rpctest.o: rpctest.cc $(INCLS)

libext.a: sdr.o xgml.o json.o
	ar cru libext.a sdr.o xgml.o json.o
	ranlib libext.a

libcore.a: osp.o cthread.o cdisp.o osptimer.o
	ar cru libcore.a osp.o cthread.o cdisp.o osptimer.o
	ranlib libcore.a

librpc.a: rpc.o restcall.o
	ar cru librpc.a rpc.o restcall.o
	ranlib librpc.a

rpctest: rpctest.o librpc.a libext.a libcore.a
	c++ $(OSXVERSION) -o rpctest rpctest.o librpc.a libext.a libcore.a -lpthread

cdisptest: cdisptest.o libcore.a
	c++ $(OSXVERSION) -o cdisptest cdisptest.o libcore.a -lpthread

timertest: timertest.o libcore.a
	c++ $(OSXVERSION) -o timertest timertest.o libcore.a -lpthread

jsontest: jsontest.o libext.a
	c++ $(OSXVERSION) -o jsontest jsontest.o libext.a

xgmltest: xgmltest.o libext.a
	c++ $(OSXVERSION) -o xgmltest xgmltest.o libext.a
