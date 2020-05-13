all: qcmd randio

INCLS=json.h osp.h

json.o: json.cc $(INCLS)
	g++ -g -c json.cc

qcmd.o: qcmd.cc $(INCLS)
	g++ -g -c qcmd.cc

randio.o: randio.cc $(INCLS)
	g++ -g -c randio.cc

qcmd: qcmd.o json.o $(INCLS)
	g++ -g -o qcmd qcmd.cc json.o -lpthread

randio: randio.o
	g++ -g -o randio randio.o

clean:
	rm -f qcmd randio
	rm -f *.o
	rm -rf qcmd.dSYM randio.dSYM
