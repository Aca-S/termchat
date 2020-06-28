#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>

#include "socketcom.h"

char *serialize_uint32_t(char *buffer, uint32_t val) {
	val = htonl(val);
	buffer[0] = val >> 24;
	buffer[1] = val >> 16;
	buffer[2] = val >> 8;
	buffer[3] = val >> 0;
	return buffer;
}

char *serialize_struct_message(char *buffer, message *msg) {
	int offset = 0;
	serialize_uint32_t(buffer, msg->type);
	offset += sizeof(uint32_t);
	memcpy(buffer + offset, msg->name, MAX_NAME_SIZE);
	offset += MAX_NAME_SIZE;
	serialize_uint32_t(buffer + offset, msg->payloadLength);
	offset += sizeof(uint32_t);
	memcpy(buffer + offset, msg->payload, MAX_PAYLOAD_SIZE);
	return buffer;
}

uint32_t deserialize_uint32_t(char *buffer) {
	return ntohl(buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3] << 0);
}

message deserialize_struct_message(char *buffer) {
	message msg;
	int offset = 0;
	msg.type = deserialize_uint32_t(buffer);
	offset += sizeof(uint32_t);
	memcpy(msg.name, buffer + offset, MAX_NAME_SIZE);
	offset += MAX_NAME_SIZE;
	msg.payloadLength = deserialize_uint32_t(buffer + offset);
	offset += sizeof(uint32_t);
	memcpy(msg.payload, buffer + offset, MAX_PAYLOAD_SIZE);
	return msg;
}

int sanitize(message *msg) {
	int count = 0, twsFlag = 1;
	for(int i = 0; i < msg->payloadLength; i++)
	{
		if(msg->payload[i] >= 32 && msg->payload[i] <= 127)
		{
			if(msg->payload[i] == ' ' && twsFlag)
				continue;
			if(i != 0 && msg->payload[i] == ' ' && msg->payload[i - 1] == ' ')
				continue;
			msg->payload[count] = msg->payload[i];
			count++;
			twsFlag = 0;
		}
	}
	msg->payloadLength = count;
	msg->payload[count] = '\0';
	return count;
}

int sendByteStream(int socketFD, char *buffer, int length) {
	int sent = 0, sentTotal = 0;
	while((sent = send(socketFD, buffer + sentTotal, length - sentTotal, 0)) > 0)
		sentTotal += sent;
	if(sent == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
		return -1;
	return sentTotal;
}

int receiveByteStream(int socketFD, char *buffer, int length) {
	int received = 0, receivedTotal = 0;
	while((received = recv(socketFD, buffer + receivedTotal, length - receivedTotal, 0)) > 0)
		receivedTotal += received;
	buffer[receivedTotal] = '\0';
	if(received == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
		return -1;
	return receivedTotal;
}

int receiveMessageStream(int socketFD, char *buffer) {
	int prefix = receiveByteStream(socketFD, buffer, MESSAGE_PREFIX_SIZE);
	if(prefix <= 0)
		return prefix;
	uint32_t payloadLength = deserialize_uint32_t(buffer + 4 + MAX_NAME_SIZE);
	if(payloadLength >= MAX_PAYLOAD_SIZE)
		return -1;
	int received = receiveByteStream(socketFD, buffer + MESSAGE_PREFIX_SIZE, payloadLength);
	if(received == -1)
		return -1;
	return prefix + received;
}

int sendMessageStream(int socketFD, uint32_t type, char *name, char *payload) {
	message msg;
	msg.type = type;
	strcpy(msg.name, name);
	if(payload == NULL)
		msg.payloadLength = 0;	
	else
	{
		msg.payloadLength = strlen(payload);
		strcpy(msg.payload, payload);
	}
	char buffer[TOTAL_BUFFER_SIZE];
	serialize_struct_message(buffer, &msg);
	return sendByteStream(socketFD, buffer, MESSAGE_PREFIX_SIZE + msg.payloadLength);
}

int setSocketNonBlocking(int socketFD) {
	int socketFlags = fcntl(socketFD, F_GETFL, 0);
	if(socketFlags == -1)
		return -1;
	if(fcntl(socketFD, F_SETFL, socketFlags | O_NONBLOCK) == -1)
		return -1;
	return 0;
}

int createListeners(int **listeners, char *portStr) {
	struct addrinfo hints;
	struct addrinfo *res, *rcur;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	hints.ai_addr = NULL;
	hints.ai_canonname = NULL;
	hints.ai_next = NULL;

	if(getaddrinfo(NULL, portStr, &hints, &res) != 0)
		return -1;

	int currentSize = 2;
	*listeners = malloc(currentSize * sizeof(int));
	if(*listeners == NULL)
		return -1;

	int counter = 0;
	for(rcur = res; rcur != NULL; rcur = rcur->ai_next)
	{
		if(counter == currentSize)
		{
			currentSize *= 2;
			*listeners = realloc(*listeners, currentSize * sizeof(int));
			if(*listeners == NULL)
				return -1;
		}
		(*listeners)[counter] = socket(rcur->ai_family, rcur->ai_socktype, rcur->ai_protocol);
		if((*listeners)[counter] == -1)
			continue;
		if(setSocketNonBlocking((*listeners)[counter]) == -1)
		{
			close((*listeners)[counter]);
			continue;
		}
		int socketOptions = 1;
		if(setsockopt((*listeners)[counter], SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &socketOptions, sizeof(socketOptions)) == -1)
		{
			close((*listeners)[counter]);
			continue;
		}
		if(bind((*listeners)[counter], rcur->ai_addr, rcur->ai_addrlen) == 0)
		{
			if(listen((*listeners)[counter], 128) == -1)
				close((*listeners)[counter]);
			else
				counter++;
		}
	}
	freeaddrinfo(res);
	return (counter == 0) ? -1 : counter;
}

int acceptConnection(int listeningSocketFD) {
	struct sockaddr_storage clientAddress;
	memset(&clientAddress, 0, sizeof(struct sockaddr_storage));
	socklen_t addressLength = sizeof(struct sockaddr_storage);
	int clientSocketFD = accept(listeningSocketFD, (struct sockaddr*)&clientAddress, &addressLength);
	if(clientSocketFD == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
		return -1;
	if(setSocketNonBlocking(clientSocketFD) == -1)
		return -1;
	return clientSocketFD;
}

int connectToServer(char *addressStr, char *portStr) {
	struct addrinfo hints;
	struct addrinfo *res, *rcur;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_flags = 0;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	hints.ai_addr = NULL;
	hints.ai_canonname = NULL;
	hints.ai_next = NULL;

	if(getaddrinfo(addressStr, portStr, &hints, &res) != 0)
		return -1;

	int socketFD, flag = 0;
	for(rcur = res; rcur != NULL; rcur = rcur->ai_next)
	{
		socketFD = socket(rcur->ai_family, rcur->ai_socktype, rcur->ai_protocol);
		if(socketFD == -1)
			continue;
		if(setSocketNonBlocking(socketFD) == -1)
		{
			close(socketFD);
			continue;
		}
		int status = connect(socketFD, rcur->ai_addr, rcur->ai_addrlen);
		if(status == 0 || (status == -1 && errno == EINPROGRESS))
		{
			flag = 1;
			break;
		}
		/* if we get here it means connect failed */
		close(socketFD);
	}
	freeaddrinfo(res);
	return (flag == 0) ? -1 : socketFD;
}

int printPeerInfo(int socketFD) {
	struct sockaddr_storage address;
	memset(&address, 0, sizeof(struct sockaddr_storage));
	socklen_t addressLength = sizeof(struct sockaddr_storage);
	if(getpeername(socketFD, (struct sockaddr*)&address, &addressLength) == -1)
		return -1;
	char host[NI_MAXHOST], service[NI_MAXSERV];
	if(getnameinfo((struct sockaddr*)&address, addressLength, host, sizeof(host), service, sizeof(service), 0) == -1)
		return -1;;
	printf("address: %s, port: %s\n", host, service);
	return 0;
}

int sendRegMsg(int socketFD, char *name, char *text) {
	return sendMessageStream(socketFD, REG_MSG, name, text);
}

int sendConReq(int socketFD, char *name) {
	return sendMessageStream(socketFD, REQ_CON, name, NULL);
}

int sendConRes(int socketFD, char *name) {
	return sendMessageStream(socketFD, RES_CON, name, NULL);
}

int sendNicReq(int socketFD, char *name, char *nick) {
	return sendMessageStream(socketFD, REQ_NIC, name, nick);
}

int sendNicRes(int socketFD, char *name, char *nick) {
	return sendMessageStream(socketFD, RES_NIC, name, nick);
}

int sendDisSig(int socketFD, char *name) {
	return sendMessageStream(socketFD, SIG_DIS, name, NULL);
}
