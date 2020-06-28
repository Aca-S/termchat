CC = gcc

client: client.c socketcom.c advuiel.c
	$(CC) client.c socketcom.c advuiel.c -lncurses -o client

server: server.c socketcom.c
	$(CC) server.c socketcom.c -o server
