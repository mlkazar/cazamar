all: mfand libmf.a liboauth.a libjsdb.a librst.a libcfs.a libupload.a liblfs.a libstream.a libupnp.a mfanc strload ssls sslc jsdbtest upnptest xapitest idtest sapitest apptest keyserv cfstest walktest uptest scantest jwttest

install: all *.h
	cp -p *.h ../include/.
	cp -p *.a ../lib/.

clean:
	rm -f *.o *.a mfand mfanc stream strload ssls sslc jsdbtest \
          upnptest rcv.mp3 xapitest idtest sapitest apptest cfstest keyserv \
	  walktest uptest scantest stations.checked jwttest

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

RSTINCLS=rst.h bufsocket.h bufgen.h buftls.h jsdb.h buffactory.h jwt.h

INCLS=../include/*.h mfclient.h mfdata.h mfand.h $(RSTINCLS) radiostream.h xapi.h xapipool.h \
strdb.h upnp.h radixtree.h streams.h sapi.h sapilogin.h upload.h radioscan.h

liboauth.a: oahash.o oauth.o oasha1.o oapass.o oaxmalloc.o
	ar cru liboauth.a oahash.o oauth.o oasha1.o oapass.o oaxmalloc.o 

libupnp.a: upnp.o radixtree.o
	ar cru libupnp.a upnp.o radixtree.o
	ranlib libupnp.a

libmf.a: mfclient.o 
	ar cru libmf.a mfclient.o 
	ranlib libmf.a

librst.a: rst.o bufsocket.o buftls.o bufgen.o xapi.o xapipool.o sapi.o sapilogin.o jwt.o
	ar cru librst.a rst.o bufsocket.o buftls.o bufgen.o xapi.o xapipool.o sapi.o sapilogin.o jwt.o
	ranlib librst.a

libjsdb.a: jsdb.o
	ar cru libjsdb.a jsdb.o 
	ranlib libjsdb.a

liblfs.a: walkdisp.o
	ar cru liblfs.a walkdisp.o
	ranlib liblfs.a

libcfs.a: cfs.o cfsms.o
	ar cru libcfs.a cfs.o cfsms.o
	ranlib libcfs.a

libupload.a: upload.o
	ar cru libupload.a upload.o
	ranlib libupload.a

rst.o: $(RSTINCLS)

radiostream.o: radiostream.cc $(INCLS)

radioscan.o: radioscan.cc $(INCLS)

streams.o: streams.cc $(INCLS)

id.o: id.cc $(INCLS)

idtest.o: idtest.cc $(INCLS)

jwttest.o: jwttest.cc $(INCLS)

xapi.o: xapi.cc $(INCLS)

xapipool.o: xapipool.cc $(INCLS)

sapi.o: sapi.cc $(INCLS)

sapilogin.o: sapilogin.cc $(INCLS)

mfand.o: mfand.cc $(INCLS)

mfanc.o: mfanc.cc $(INCLS)

strdb.o: strdb.cc $(INCLS)

strload.o: strload.cc $(INCLS)

ssls.o: ssls.cc $(INCLS)

sslc.o: sslc.cc $(INCLS)

bufsocket.o: bufsocket.cc $(INCLS)

buftls.o: buftls.cc $(INCLS)

bufgen.o: bufgen.cc $(INCLS)

jsdb.o: jsdb.cc $(INCLS)

jsdbtest.o: jsdbtest.cc $(INCLS)

upnp.o: upnp.cc $(INCLS)

upnptest.o: upnptest.cc $(INCLS)

radixtree.o: radixtree.cc $(INCLS)

xapitest.o: xapitest.cc $(INCLS)

sapitest.o: sapitest.cc $(INCLS)

keyserv.o: keyserv.cc $(INCLS)

apptest.o: apptest.cc $(INCLS)

cfstest.o: cfstest.cc $(INCLS)

uptest.o: uptest.cc $(INCLS)

walktest.o: walktest.cc $(INCLS)

jsdbtest: jsdbtest.o libjsdb.a ../lib/libext.a ../lib/libcore.a
	c++ $(OSVERSION) -o jsdbtest jsdbtest.o libjsdb.a ../lib/libext.a ../lib/libcore.a

strdb.o: strdb.cc $(INCLS)

scantest.o: scantest.cc $(INCLS)

radioscan.o: radioscan.cc $(INCLS)

strload: strload.o strdb.o radiostream.o libstream.a librst.a liboauth.a libjsdb.a ../lib/libext.a ../lib/libcore.a
	c++ $(OSXVERSION) -o strload strload.o strdb.o radiostream.o libstream.a \
	    librst.a liboauth.a libjsdb.a ../lib/libext.a ../lib/libcore.a -lpthread

scantest: scantest.o libstream.a librst.a  libjsdb.a ../lib/libcore.a ../lib/libext.a
ifeq ($(OS),Linux)
	c++ $(OSXVERSION) -o scantest scantest.o libstream.a librst.a  libjsdb.a ../lib/libcore.a ../lib/libext.a -lssl -lcrypto -lpthread
else
	c++ $(OSXVERSION) -o scantest scantest.o libstream.a librst.a  libjsdb.a ../lib/libcore.a ../lib/libext.a /usr/local/ssl/lib/libssl.a /usr/local/ssl/lib/libcrypto.a -lpthread
endif

ifeq ($(OS),Linux)
xapitest: xapitest.o librst.a ../lib/libext.a ../lib/libcore.a
	c++ $(OSXVERSION) -o xapitest xapitest.o librst.a ../lib/libext.a ../lib/libcore.a -lssl -lcrypto -lpthread
else
xapitest: xapitest.o librst.a ../lib/libext.a ../lib/libcore.a /usr/local/ssl/lib/libssl.a /usr/local/ssl/lib/libcrypto.a
	c++ $(OSXVERSION) -o xapitest xapitest.o librst.a ../lib/libext.a ../lib/libcore.a /usr/local/ssl/lib/libssl.a /usr/local/ssl/lib/libcrypto.a
endif

ifeq ($(OS),Linux)
sapitest: sapitest.o librst.a ../lib/libext.a ../lib/libcore.a
	c++ $(OSXVERSION) -o sapitest sapitest.o librst.a ../lib/libext.a ../lib/libcore.a -lssl -lcrypto -lpthread
else
sapitest: sapitest.o librst.a ../lib/libext.a ../lib/libcore.a /usr/local/ssl/lib/libssl.a /usr/local/ssl/lib/libcrypto.a
	c++ $(OSXVERSION) -o sapitest sapitest.o librst.a ../lib/libext.a ../lib/libcore.a /usr/local/ssl/lib/libssl.a /usr/local/ssl/lib/libcrypto.a
endif

ifeq ($(OS),Linux)
keyserv: keyserv.o librst.a ../lib/libext.a ../lib/libcore.a
	c++ $(OSXVERSION) -o keyserv keyserv.o librst.a ../lib/libext.a ../lib/libcore.a -lssl -lcrypto -lpthread
else
keyserv: keyserv.o librst.a ../lib/libext.a ../lib/libcore.a /usr/local/ssl/lib/libssl.a /usr/local/ssl/lib/libcrypto.a
	c++ $(OSXVERSION) -o keyserv keyserv.o librst.a ../lib/libext.a ../lib/libcore.a /usr/local/ssl/lib/libssl.a /usr/local/ssl/lib/libcrypto.a
endif

ifeq ($(OS),Linux)
apptest: apptest.o librst.a ../lib/libext.a ../lib/libcore.a
	c++ $(OSXVERSION) -o apptest apptest.o librst.a ../lib/libext.a ../lib/libcore.a -lssl -lcrypto -lpthread
else
apptest: apptest.o librst.a ../lib/libext.a ../lib/libcore.a /usr/local/ssl/lib/libssl.a /usr/local/ssl/lib/libcrypto.a
	c++ $(OSXVERSION) -o apptest apptest.o librst.a ../lib/libext.a ../lib/libcore.a /usr/local/ssl/lib/libssl.a /usr/local/ssl/lib/libcrypto.a
endif

ifeq ($(OS),Linux)
cfstest: cfstest.o libcfs.a librst.a ../lib/libext.a ../lib/libcore.a
	c++ $(OSXVERSION) -o cfstest cfstest.o libcfs.a librst.a ../lib/libext.a ../lib/libcore.a -lssl -lcrypto -lpthread
else
cfstest: cfstest.o libcfs.a librst.a ../lib/libext.a ../lib/libcore.a /usr/local/ssl/lib/libssl.a /usr/local/ssl/lib/libcrypto.a
	c++ $(OSXVERSION) -o cfstest cfstest.o libcfs.a librst.a ../lib/libext.a ../lib/libcore.a /usr/local/ssl/lib/libssl.a /usr/local/ssl/lib/libcrypto.a
endif

ifeq ($(OS),Linux)
uptest: uptest.o libupload.a libcfs.a liblfs.a  librst.a ../lib/libext.a ../lib/libcore.a
	c++ $(OSXVERSION) -o uptest uptest.o libupload.a libcfs.a liblfs.a librst.a ../lib/libext.a ../lib/libcore.a -lssl -lcrypto -lpthread
else
uptest: uptest.o libupload.a libcfs.a liblfs.a librst.a ../lib/libext.a ../lib/libcore.a /usr/local/ssl/lib/libssl.a /usr/local/ssl/lib/libcrypto.a
	c++ $(OSXVERSION) -o uptest uptest.o libupload.a libcfs.a liblfs.a librst.a ../lib/libext.a ../lib/libcore.a /usr/local/ssl/lib/libssl.a /usr/local/ssl/lib/libcrypto.a
endif

ifeq ($(OS),Linux)
walktest: walktest.o liblfs.a ../lib/libext.a ../lib/libcore.a
	c++ $(OSXVERSION) -o walktest walktest.o liblfs.a ../lib/libext.a ../lib/libcore.a -lpthread
else
walktest: walktest.o liblfs.a ../lib/libext.a ../lib/libcore.a
	c++ $(OSXVERSION) -o walktest walktest.o liblfs.a ../lib/libext.a ../lib/libcore.a
endif

libstream.a: radiostream.o radioscan.o
	ar cru libstream.a radiostream.o radioscan.o
	ranlib libstream.a

mfand: mfand.o ../lib/librpc.a libmf.a ../lib/libext.a ../lib/libcore.a
	c++ $(OSXVERSION) -o mfand mfand.o libmf.a ../lib/librpc.a ../lib/libext.a ../lib/libcore.a -lcurl -lpthread

mfanc: mfanc.o ../lib/librpc.a ../lib/libext.a ../lib/libcore.a libmf.a
	c++ $(OSXVERSION) -o mfanc mfanc.o libmf.a ../lib/librpc.a ../lib/libext.a ../lib/libcore.a -lpthread

ifeq ($(OS),Linux)
ssls: ssls.o
	c++ $(OSXVERSION) -o ssls ssls.o -lssl -lcrypto
else
ssls: ssls.o /usr/local/ssl/lib/libssl.a /usr/local/ssl/lib/libcrypto.a
	c++ $(OSXVERSION) -o ssls ssls.o /usr/local/ssl/lib/libssl.a /usr/local/ssl/lib/libcrypto.a
endif

ifeq ($(OS),Linux)
sslc: sslc.o
	c++ $(OSXVERSION) -o sslc sslc.o -lssl -lcrypto
else
sslc: sslc.o /usr/local/ssl/lib/libssl.a /usr/local/ssl/lib/libcrypto.a
	c++ $(OSXVERSION) -o sslc sslc.o /usr/local/ssl/lib/libssl.a /usr/local/ssl/lib/libcrypto.a
endif

upnptest: upnptest.o librst.a libupnp.a ../lib/libext.a ../lib/libcore.a liboauth.a
	c++ $(OXSVERSION) -o upnptest upnptest.o librst.a libupnp.a ../lib/libext.a ../lib/libcore.a liboauth.a -lpthread

idtest: idtest.o id.o streams.o
	c++ $(OSXVERSION) -o idtest idtest.o id.o streams.o ../lib/libcore.a ../lib/librpc.a

jwttest: jwttest.o librst.a ../lib/libext.a
	c++ $(OSXVERSION) -o jwttest jwttest.o librst.a ../lib/libext.a
