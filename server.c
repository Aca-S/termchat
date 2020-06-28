#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define PORT "8080"
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

	Note: There's usually two listeners, one for ipv4 and one for ipv6
*/

void initServer(struct server *server) {
	int *listeners;
	int numOfListeners = createListeners(&listeners, PORT);
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

void killClient(struct server *server, int clientId) {
	for(int i = server->numOfListeners; i < server->numOfMonitors; i++)
	{
		if(i != clientId)
			sendDisSig(server->monitors[i].fd, server->clients[clientId].name);
	}
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
	struct server server;
	initServer(&server);

	printf("Server successfully started\n");
 
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
						sendRegMsg(server.monitors[server.numOfMonitors - 1].fd, "SERVER", "To set a name, do /nick <name>");
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
					if(msg.type == REG_MSG && sanitize(&msg) == 0)
						continue;

					/* We're checking to see if the client name has been tampered with */
					if(strcmp(msg.name, server.clients[i].name) != 0)
						continue;

					switch(msg.type)
					{
						case REG_MSG:
							for(int j = server.numOfListeners; j < server.numOfMonitors; j++)
								sendRegMsg(server.monitors[j].fd, server.clients[i].name, msg.payload);
							break;
						case REQ_CON:
							strcpy(server.clients[i].name, msg.name);
							for(int j = server.numOfListeners; j < server.numOfMonitors; j++)
							{
								if(j != i)
									sendConRes(server.monitors[j].fd, server.clients[i].name);
								sendConRes(server.monitors[i].fd, server.clients[j].name);
							}
							break;
						case REQ_NIC:
							for(int j = server.numOfListeners; j < server.numOfMonitors; j++)
								sendNicRes(server.monitors[j].fd, server.clients[i].name, msg.payload);
							strcpy(server.clients[i].name, msg.payload);
							break;
					}
				}
			}
		}
	}

	/* We never get here */
	killServer(&server);
	exit(EXIT_SUCCESS);
}
