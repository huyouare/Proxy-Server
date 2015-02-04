all:
	gcc -c csapp.c
	gcc -c proxy.c
	gcc -pthread csapp.o proxy.o -o proxy
	