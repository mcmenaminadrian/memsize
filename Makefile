default: all

all: memsize 

clean:
	rm -f *.o

memsize: memsize.o
	g++ -O2 -o memsize -Wall memsize.o -lexpat -lpthread

memsize.o: memsize.cpp
	g++ -O2 -o memsize.o -c -Wall memsize.cpp

debug: dmemsize.o
	g++ -g -o memsize -Wall dmemsize.o -lexpat -lpthread

dmemsize.o: memsize.cpp
	g++ -g -o dmemsize.o -c -Wall memsize.cpp
