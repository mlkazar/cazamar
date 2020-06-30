all:
	-mkdir include lib
	(cd rpc;make install); (cd mfand; make install); (cd radio; make install)

setup:
	-mkdir include lib

install: all

clean:
	(cd rpc;make clean); (cd mfand; make clean); (cd radio; make clean); (cd repl; make clean)
	-rm -rf include lib

beancount:
	wc rpc/*.[ch]* mfand/*.[ch]* mfand/*.html MusicFan/MusicFan/*.[mhc]* Upload/Upload/*.[mhc]* radio/*.{cc,h}
