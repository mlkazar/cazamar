all:
	(cd rpc;make install); (cd mfand; make install); (cd radio; make install)

install: all

clean:
	(cd rpc;make clean); (cd mfand; make clean); (cd radio; make clean)

beancount:
	wc rpc/*.[ch]* mfand/*.[ch]* mfand/*.html MusicFan/MusicFan/*.[mhc]*
