all:
	-mkdir include lib
	(cd rpc;make install); (cd voter; make install); (cd fin;make install); (cd mfand; make install); (cd radio; make install); (cd vaccine; make install)

setup:
	-mkdir include lib

install: all

clean:
	(cd rpc;make clean); (cd voter; make clean); (cd fin;make clean);(cd mfand; make clean); (cd radio; make clean); (cd repl; make clean); (cd vaccine; make clean)
	-rm -rf include lib

beancount:
	wc rpc/*.[ch]* voter/*.[ch]* fin/*.[ch]* mfand/*.[ch]* mfand/*.html MusicFan/MusicFan/*.[mhc]* Upload/Upload/*.[mhc]* radio/*.{cc,h} repl/*.{cc,h} vaccine/*.cc
