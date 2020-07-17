#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "socketcom.h"

#define checkError(expression, errorMessage)\
do\
{\
	if(expression)\
	{\
		perror(errorMessage);\
		exit(EXIT_FAILURE);\
	}\
} while(0);

#define DEFAULT_PORT "8080"
#define MAX_CONNECTIONS 256

struct client {
	char name[MAX_NAME_SIZE];
};

struct server {
	int numOfListeners;
	int numOfMonitors;
	struct pollfd *monitors;
	struct client *clients;
};

/*
	          Listeners
	MONITORS: 0 1 ... k | k + 1 2 3 4 5 6 7 8 9 ...
	 CLIENTS: x x ... x | k + 1 2 3 4 5 6 7 8 9 ...
	            First client--^

	Note: There's usually two listeners, one for IPv4 and one for IPv6
*/

void initServer(struct server *server, char *port) {
	int *listeners;
	int numOfListeners = createListeners(&listeners, port);
	checkError(numOfListeners == -1, "SERVER INIT FATAL ERROR - createListeners");

	server->monitors = malloc((numOfListeners + MAX_CONNECTIONS) * sizeof(struct pollfd));
	checkError(server->monitors == NULL, "SERVER INIT FATAL ERROR - monitors malloc");
	memset(server->monitors, 0, sizeof(server->monitors));

	server->clients = malloc((numOfListeners + MAX_CONNECTIONS) * sizeof(struct client));
	checkError(server->clients == NULL, "SERVER INIT FATAL ERROR - clients malloc");
	memset(server->clients, 0, sizeof(server->clients));

	for(int i = 0; i < numOfListeners; i++)
	{
		server->monitors[i].fd = listeners[i];
		server->monitors[i].events = POLLIN;
	}
	server->numOfListeners = numOfListeners;
	server->numOfMonitors = numOfListeners;
	free(listeners);
}

void killServer(struct server *server) {
	server->numOfListeners = 0;
	server->numOfMonitors = 0;
	free(server->monitors);
	free(server->clients);
}

int acceptClient(struct server *server, int listeningSocketFD) {
	int clientSocketFD = acceptConnection(listeningSocketFD);
	if(clientSocketFD == -1)
		return -1;
	if(server->numOfMonitors - server->numOfListeners == MAX_CONNECTIONS)
	{
		close(clientSocketFD);
		return -1;
	}
	server->monitors[server->numOfMonitors].fd = clientSocketFD;
	server->monitors[server->numOfMonitors].events = POLLIN;
	server->monitors[server->numOfMonitors].revents = 0;
	strcpy(server->clients[server->numOfMonitors].name, "CLIENT");
	server->numOfMonitors++;
	return clientSocketFD;
}

/*
	The optional args should represent the IDs of clients which not to broadcast to.
	They have to be listed in rising order and have to end with a negative value.
*/
int broadcast(struct server *server, uint32_t type, char *name, char *payload, ...) {
	va_list excludes;
	va_start(excludes, payload);
	int exclude = va_arg(excludes, int);
	for(int i = server->numOfListeners; i < server->numOfMonitors; i++)
	{
		if(i == exclude)
		{
			exclude = va_arg(excludes, int);
			continue;
		}
		if(sendMessageStream(server->monitors[i].fd, type, name, payload) == -1)
		{
			va_end(excludes);
			return -1;
		}
	}
	va_end(excludes);
	return 0;
}

int sendByName(struct server *server, char* target, uint32_t type, char *name, char *payload) {
	for(int i = server->numOfListeners; i < server->numOfMonitors; i++)
	{
		if(strcmp(target, server->clients[i].name) == 0)
			return sendMessageStream(server->monitors[i].fd, type, name, payload);
	}
	return -1;
}

void killClient(struct server *server, int clientId) {
	broadcast(server, SIG_DIS, server->clients[clientId].name, NULL, clientId, -1);
	close(server->monitors[clientId].fd);
	server->monitors[clientId].fd = -1;
	for(int i = clientId; i < server->numOfMonitors - 1; i++)
	{
		server->monitors[i].fd = server->monitors[i + 1].fd;
		server->clients[i] = server->clients[i + 1];
	}
	server->numOfMonitors--;
}

int main(int argc, char *argv[]) {

	/* Server init */
	char *port = DEFAULT_PORT;
	if(argc >= 2)
		port = argv[1];

	struct server server;
	initServer(&server, port);

	printf("Server successfully started on port %s\n", port);
 
	while(1)
	{
		checkError(poll(server.monitors, server.numOfMonitors, -1) == -1, "poll");
		int currentConnections = server.numOfMonitors;

		/* Looping through all the active monitors */
		for(int i = 0; i < currentConnections; i++)
		{
			/* If there is activity on the current monitor */
			if(server.monitors[i].revents & POLLIN)
			{
				/* And it's from one of the listening sockets */
				if(i < server.numOfListeners)
				{
					/* It means that we have a new connection, so we're attempting to accepting it */
					if(acceptClient(&server, server.monitors[i].fd) != -1)
					{
						printf("New connection from ");
						checkError(printPeerInfo(server.monitors[server.numOfMonitors - 1].fd) == -1, "printPeerInfo");
						sendMessageStream(server.monitors[server.numOfMonitors - 1].fd, REG_MSG, "SERVER", "To set a name, do /nick <name>");
					}
					else
						printf("A client failed to connect to the server\n");
				}
				/* If it's not from the listening socket monitor */
				else
				{
					/* It's from one of the clients, so we're attempting to receive a message */
					char buffer[TOTAL_BUFFER_SIZE];
					int receivedTotal = receiveMessageStream(server.monitors[i].fd, buffer);

					/* If client disconnected / there was an error reading from it, close its socket and compress arrays */
					if(receivedTotal == 0 || receivedTotal == -1)
					{
						printf("Client disconnected from ");
						checkError(printPeerInfo(server.monitors[i].fd) == -1, "printPeerInfo");
						killClient(&server, i);
						currentConnections--;
						continue;
					}

					/* We're deserializing the message so we can check its type and decide what to do with it */
					message msg = deserialize_struct_message(buffer);
					if(sanitize(&msg) == 0 && (msg.type == REG_MSG || msg.type == PRV_MSG))
						continue;

					/* We're checking to see if the client name has been tampered with */
					if(strcmp(msg.name, server.clients[i].name) != 0)
						continue;

					switch(msg.type)
					{
						case REG_MSG:
							broadcast(&server, REG_MSG, server.clients[i].name, msg.payload, -1);
							break;
						case REQ_CON:
							strcpy(server.clients[i].name, msg.name);
							broadcast(&server, RES_CON, server.clients[i].name, NULL, i, -1);
							for(int j = server.numOfListeners; j < server.numOfMonitors; j++)
								sendMessageStream(server.monitors[i].fd, RES_CON, server.clients[j].name, NULL);
							break;
						case REQ_NIC:
							broadcast(&server, RES_NIC, server.clients[i].name, msg.payload, -1);
							readArgs(msg.payload, server.clients[i].name, NULL);
							break;
						case PRV_MSG:
						{
							char target[MAX_NAME_SIZE];
							int len = readArgs(msg.payload, target, NULL);
							if(sendByName(&server, target, PRV_MSG, server.clients[i].name, msg.payload + len + 1) != -1)
								sendMessageStream(server.monitors[i].fd, PRV_MSG, server.clients[i].name, msg.payload + len + 1);
							break;
						}
					}
				}
			}
		}
	}

	/* We never get here */
	killServer(&server);
	exit(EXIT_SUCCESS);
}
