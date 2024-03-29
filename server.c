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

void initServer(struct server *server, char *port);
void killServer(struct server *server);
int acceptClient(struct server *server, int listeningSocketFD);
void killClient(struct server *server, int clientId);
int broadcast(struct server *server, uint32_t type, char *name, char *payload, ...);
int findByName(struct server *server, char *target);

int handleRegular(struct server *server, message *msg, int id);
int handlePrivate(struct server *server, message *msg, int id);
int handleConnect(struct server *server, message *msg, int id);
int handleNickname(struct server *server, message *msg, int id);

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
						sendMessageStream(server.monitors[server.numOfMonitors - 1].fd, SIG_M | REG_F, "SERVER", "To set a name, do /nick <name>");
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

					/* Remove all non-alphanumeric characters from the message payload */
					if(sanitize(&msg) == 0 && (msg.type == (REQ_M | REG_F) || msg.type == (REQ_M | PRV_F)))
						continue;

					/* The server only receives requests and nothing else */
					if((msg.type & MASK_M) != REQ_M)
						continue;

					/* We're checking to see if the client name has been tampered with */
					if(strcmp(msg.name, server.clients[i].name) != 0)
						continue;

					switch(msg.type & MASK_F)
					{
						case REG_F:
							handleRegular(&server, &msg, i);
							break;
						case PRV_F:
							handlePrivate(&server, &msg, i);
							break;
						case CON_F:
							handleConnect(&server, &msg, i);
							break;
						case NIC_F:
							handleNickname(&server, &msg, i);
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

void initServer(struct server *server, char *port) {
	int *listeners;
	int numOfListeners = createListeners(&listeners, port);
	checkError(numOfListeners == -1, "SERVER INIT FATAL ERROR - createListeners");
	
	int totalNumOfMonitors = numOfListeners + MAX_CONNECTIONS;

	server->monitors = malloc(totalNumOfMonitors * sizeof(struct pollfd));
	checkError(server->monitors == NULL, "SERVER INIT FATAL ERROR - monitors malloc");
	memset(server->monitors, 0, sizeof(totalNumOfMonitors * sizeof(struct pollfd)));

	/* 
		We want to keep the monitor and client array indexes aligned to each other
		for simplicity's sake, so we're allocating totalNumOfMonitors clients
		and keeping the first numOfListeners clients unused.
	*/
	server->clients = malloc(totalNumOfMonitors * sizeof(struct client));
	checkError(server->clients == NULL, "SERVER INIT FATAL ERROR - clients malloc");
	memset(server->clients, 0, sizeof(totalNumOfMonitors * sizeof(struct pollfd)));

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
	broadcast(server, SIG_M | DIS_F, server->clients[clientId].name, NULL, clientId, -1);
	close(server->monitors[clientId].fd);
	server->monitors[clientId].fd = -1;
	for(int i = clientId; i < server->numOfMonitors - 1; i++)
	{
		server->monitors[i].fd = server->monitors[i + 1].fd;
		server->clients[i] = server->clients[i + 1];
	}
	server->numOfMonitors--;
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
		/* We're not checking if sending to a client failed - maybe they DC-ed in the middle of the broadcast */
		sendMessageStream(server->monitors[i].fd, type, name, payload);
	}
	va_end(excludes);
	return 0;
}

int findByName(struct server *server, char *target) {
	for(int i = server->numOfListeners; i < server->numOfMonitors; i++)
	{
		if(strcmp(target, server->clients[i].name) == 0)
			return i;
	}
	return -1;
}

int handleRegular(struct server *server, message *msg, int id) {
	return broadcast(server, SIG_M | REG_F, server->clients[id].name, msg->payload, -1);
}

int handlePrivate(struct server *server, message *msg, int id) {
	char target[MAX_NAME_SIZE];
	int len = readArgs(msg->payload, target, NULL);
	if(len != -1)
	{
		int targetID = findByName(server, target);
		if(targetID != -1 && sendMessageStream(server->monitors[targetID].fd, SIG_M | PRV_F, server->clients[id].name, msg->payload + len + 1) != -1)
		{
			sendMessageStream(server->monitors[id].fd, RES_M | SCS_S | PRV_F, server->clients[targetID].name, msg->payload + len + 1);
			return 0;
		}
	}
	sendMessageStream(server->monitors[id].fd, RES_M | FLR_S | PRV_F, server->clients[id].name, target);
	return -1;
}

int handleConnect(struct server *server, message *msg, int id) {
	strcpy(server->clients[id].name, msg->name);
	broadcast(server, SIG_M | CON_F, server->clients[id].name, NULL, id, -1);
	for(int j = server->numOfListeners; j < server->numOfMonitors; j++)
		sendMessageStream(server->monitors[id].fd, SIG_M | CON_F, server->clients[j].name, NULL);
	return 0;
}

int handleNickname(struct server *server, message *msg, int id) {
	char newNick[MAX_NAME_SIZE];
	if(readArgs(msg->payload, newNick, NULL) != -1)
	{
		if(sendMessageStream(server->monitors[id].fd, RES_M | SCS_S | NIC_F, server->clients[id].name, newNick) != -1)
		{
			broadcast(server, SIG_M | NIC_F, server->clients[id].name, msg->payload, -1);
			strcpy(server->clients[id].name, newNick);
			return 0;
		}
	}
	sendMessageStream(server->monitors[id].fd, RES_M | FLR_S | NIC_F, server->clients[id].name, newNick);
	return -1;
}
