#ifndef _SOCKETCOM_H_
#define _SOCKETCOM_H_

#include <stdint.h>

#define MAX_NAME_SIZE 32
#define MESSAGE_PREFIX_SIZE (4 + MAX_NAME_SIZE + 4)
#define MAX_PAYLOAD_SIZE 1024
#define TOTAL_BUFFER_SIZE (MESSAGE_PREFIX_SIZE + MAX_PAYLOAD_SIZE)

/*
	The message structure is as follows:
	|TYPE - 4 bytes|NAME - MAX_NAME_SIZE bytes|PAYLOAD LENGTH - 4 bytes|PAYLOAD - MAX_PAYLOAD_SIZE bytes|
	|------------------------------PREFIX------------------------------|------------PAYLOAD-------------|

	Note: When receiving a message, we first receive the prefix (MESSAGE_PREFIX_SIZE) bytes, read it, and
	then receive another PAYLOAD LENGTH bytes
*/

/* Main message types - first 4 bits of the type indicator */
#define MASK_M 0xF
#define REQ_M 0
#define RES_M 1
#define SIG_M 2

/* Message statuses - second 4 bits of the type indicator */
#define MASK_S 0xF0
#define SCS_S 16
#define FLR_S 32

/* Message subtypes - final 24 bits of the type indicator */
#define MASK_F 0xFFFFFF00
#define REG_F 256
#define PRV_F 512
#define CON_F 768
#define DIS_F 1024
#define NIC_F 1280

typedef struct {
	uint32_t type;
	char name[MAX_NAME_SIZE];
	uint32_t payloadLength;
	char payload[MAX_PAYLOAD_SIZE];
} message;

/* serialization */
char *serialize_uint32_t(char *buffer, uint32_t val);
char *serialize_struct_message(char *buffer, message *msg);
uint32_t deserialize_uint32_t(char *buffer);
message deserialize_struct_message(char *buffer);
int sanitize(message *msg);

/* communication */
int receiveByteStream(int socketFD, char *buffer, int length);
int receiveMessageStream(int socketFD, char *buffer);
int sendByteStream(int socketFD, char *buffer, int length);
int sendMessageStream(int socketFD, uint32_t type, char *name, char *payload);
int readArgs(char *str, ...);

/* sockets */
int setSocketNonBlocking(int socketFD);
int createListeners(int **listeners, char *portStr);
int acceptConnection(int listeningSocketFD);
int connectToServer(char *addressStr, char *portStr);
int printPeerInfo(int socketFD);

#endif
