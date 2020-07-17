#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <poll.h>
#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <errno.h>

#include "socketcom.h"
#include "advuiel.h"

#define checkError(expression, errorMessage)\
do\
{\
	if(expression)\
	{\
		endwin();\
		perror(errorMessage);\
		exit(EXIT_FAILURE);\
	}\
} while(0);

/* Macros to help keep track of the active window */
#define ADDRESS_FIELD 0
#define PORT_FIELD 1
#define CONNECT_BUTTON 2

#define INPUT_FIELD 0
#define CHAT_WINDOW 1
#define CLIENT_LIST 2

int activeWindow = INPUT_FIELD;
char nick[MAX_NAME_SIZE] = "CLIENT";

typedef struct {
	char *commandStr;
	int (*function)(char *args, int socketFD);
} command;

int changeNick(char *args, int socketFD) {
	char newNick[MAX_NAME_SIZE];
	sscanf(args, "%*s %31s", newNick);
	sendMessageStream(socketFD, REQ_NIC, nick, newNick);
	strcpy(nick, newNick);
	return 0;
}

command commands[] =
{
	{"/nick", changeNick},
};

int numOfCommands = sizeof(commands) / sizeof(command);
int getCommandPosition(char *command) {
	for(int i = 0; i < numOfCommands; i++)
	{
		if(strcmp(command, commands[i].commandStr) == 0)
			return i;
	}
	return -1;
}

int isCommand(char *buffer) {
	return buffer[0] == '/';
}

int runCommand(char *buffer, int socketFD) {
	char commandStr[64];
	sscanf(buffer, "%63s", commandStr);
	int commandPosition = getCommandPosition(commandStr);
	if(commandPosition != -1)
		return commands[commandPosition].function(buffer, socketFD);
	return -1;
}

void printTimestamped(outputField *chatWindow, message *msg) {
	time_t secs = time(NULL);
	checkError(secs == -1, "time");
	struct tm *currentTime = localtime(&secs);
	checkError(currentTime == NULL, "localtime");
	wprintw(chatWindow->pad, "[%d:%d] %s: %s\n", currentTime->tm_hour, currentTime->tm_min, msg->name, msg->payload);

	if(chatWindow->scrollPosition == 0)
		refreshOutputField(chatWindow);
}

int main(int argc, char *argv[]) {

	/* Screen init */
	initscr();
	refresh();
	noecho();
	keypad(stdscr, TRUE);
	int terminalRows, terminalColumns;
	getmaxyx(stdscr, terminalRows, terminalColumns);
	int c;

	/* Drawing connection UI */
	WINDOW *terminalBorders = createNewWindow(terminalRows, terminalColumns, 0, 0, TRUE);

	label addressLabel;
	createLabel(&addressLabel, "Address:", terminalRows / 2 - 4, terminalColumns / 2 - 9);

	inputField addressField;
	createInputField(&addressField, 18, terminalRows / 2 - 3, terminalColumns / 2 - 9);

	label portLabel;
	createLabel(&portLabel, "Port:", terminalRows / 2, terminalColumns / 2 - 9);

	inputField portField;
	createInputField(&portField, 18, terminalRows / 2 + 1, terminalColumns / 2 - 9);

	button connectBtn;
	createButton(&connectBtn, "Connect...", terminalRows / 2 + 5, terminalColumns / 2 - 6);

	/* Waiting for connection info */
	refreshInputField(&addressField);
	int waitingForInfo = 1;
	char serverAddressStr[40], portNumberStr[6];
	activeWindow = ADDRESS_FIELD;
	while(waitingForInfo)
	{
		c = getch();
		switch(activeWindow)
		{
			case 0:
				triggerInputFieldEvent(&addressField, c);
				switch(c)
				{
					case KEY_DOWN:
						activeWindow = PORT_FIELD;
						refreshInputField(&portField);
						break;
					case '\n': case KEY_ENTER:
						strncpy(serverAddressStr, addressField.lineBuffer.buffer, sizeof(serverAddressStr) - 1);
						updateLabel(&addressLabel, serverAddressStr);
						activeWindow = PORT_FIELD;
						refreshInputField(&portField);
						break;
				}
				break;
			case 1:
				triggerInputFieldEvent(&portField, c);
				switch(c)
				{
					case KEY_UP:
						activeWindow = ADDRESS_FIELD;
						refreshInputField(&addressField);
						break;
					case KEY_DOWN:
						activeWindow = CONNECT_BUTTON;
						focusButton(&connectBtn);
						break;
					case '\n': case KEY_ENTER:
						strncpy(portNumberStr, portField.lineBuffer.buffer, sizeof(portNumberStr) - 1);
						updateLabel(&portLabel, portNumberStr);
						activeWindow = CONNECT_BUTTON;
						focusButton(&connectBtn);
						break; 
				}
				break;
			case 2:
				switch(c)
				{
					case KEY_UP:
						activeWindow = PORT_FIELD;
						unfocusButton(&connectBtn);
						refreshInputField(&portField);
						break;
					case '\n': case KEY_ENTER:
						waitingForInfo = 0;
						unfocusButton(&connectBtn);
						break;
				}
				break;
		}
	}

	/* Cleaning up windows */
	deleteLabel(&addressLabel);
	deleteInputField(&addressField);
	deleteLabel(&portLabel);
	deleteInputField(&portField);
	deleteButton(&connectBtn);

	/* Drawing chat UI */
	outputField chat;
	createOutputField(&chat, terminalRows - 3, terminalColumns - 18, 0, 0);

	inputField chatInput;
	createInputField(&chatInput, terminalColumns, terminalRows - 3, 0);

	listField clientList;
	createListField(&clientList, terminalRows - 3, 18, 0, terminalColumns - 18);

	/* Initializing connection */
	int socketFD = connectToServer(serverAddressStr, portNumberStr);
	checkError(socketFD == -1, "connectToServer");

	/* Initializing polling structures */
	struct pollfd monitors[2];
	memset(monitors, 0, sizeof(monitors));
	monitors[0].fd = STDIN_FILENO;
	monitors[0].events = POLLIN;
	monitors[1].fd = socketFD;
	monitors[1].events = POLLIN;

	/* Setting user input to be non-blocking */
	nodelay(chatInput.pad, TRUE);

	/* Setup complete - sending initial connection message to server */
	checkError(sendMessageStream(socketFD, REQ_CON, nick, NULL) == -1, "sendMessageStream");

	/* Polling for activity on either stdin or the socket */
	activeWindow = INPUT_FIELD;
	while(1)
	{
		checkError(poll(monitors, 2, -1) == -1, "poll");

		/* Activity on stdin */
		if(monitors[0].revents & POLLIN)
		{
			c = getch();
			switch(activeWindow)
			{
				case INPUT_FIELD:
					switch(c)
					{
						case KEY_UP:
							activeWindow = CHAT_WINDOW;
							break;
						case KEY_DOWN:
							if(chat.scrollPosition != 0)
								activeWindow = CHAT_WINDOW;
							break;
					}
					break;
				case CHAT_WINDOW:
					triggerOutputFieldEvent(&chat, c);
					switch(c)
					{
						case KEY_DOWN:
							if(chat.scrollPosition == 0)
								activeWindow = INPUT_FIELD;
							break;
						case KEY_RIGHT:
							focusListField(&clientList);
							activeWindow = CLIENT_LIST;
							break;
					}
					break;
				case CLIENT_LIST:
					triggerListFieldEvent(&clientList, c);
					switch(c)
					{
						case KEY_LEFT:
							unfocusListField(&clientList);
							activeWindow = CHAT_WINDOW;
							break;
					}
					break;
			}
			triggerInputFieldEvent(&chatInput, c);
			if(c == '\n' || c == KEY_ENTER)
			{
				if(isCommand(chatInput.lineBuffer.buffer))
					runCommand(chatInput.lineBuffer.buffer, socketFD);
				else
					checkError(sendMessageStream(socketFD, REG_MSG, nick, chatInput.lineBuffer.buffer) == -1, "sendMessageStream");
			}
			refreshInputField(&chatInput);
		}

		/* Activity on socket */
		if(monitors[1].revents & POLLIN)
		{
			char buffer[TOTAL_BUFFER_SIZE];
			int received;
			checkError((received = receiveMessageStream(socketFD, buffer)) == -1, "receiveMessage");
			checkError(received == 0, "Server closed connection");
			message msg = deserialize_struct_message(buffer);
			switch(msg.type)
			{
				case REG_MSG:
					printTimestamped(&chat, &msg);
					break;
				case RES_CON:
					addListFieldItem(&clientList, msg.name);
					break;
				case RES_NIC:
					replaceListFieldItem(&clientList, msg.name, msg.payload);
					break;
				case SIG_DIS:
					removeListFieldItem(&clientList, msg.name);
					break;
			}
			refreshInputField(&chatInput);
		}
	}

	/* We never get here */
	endwin();
	exit(EXIT_SUCCESS);
}
