# TermChat
A client and server implementation for Linux written entirely in C.

### Dependencies
```sudo apt-get install libncurses5-dev libncursesw5-dev```

### Building
```make server client```

### Running the server
You can either specify the port the server will run on as a CL argument (./server 5678) or just let it default to 8080 if the port's not specified.

### Running the client
Simply run it as ./client and specify the host's address and port in the text fields (enter to set them). Navigation between UI elements is done with arrow keys.

### Screenshots
![server](https://i.ibb.co/Jzx9fdX/Screenshot-from-2020-07-15-10-33-50.png)
![client](https://i.ibb.co/dJ5P5WP/Screenshot-from-2020-07-15-10-33-56.png)

### Features
- IPv4 and IPv6 support
- ncurses based client UI
- Expandable command system (work in progress)

### Future features I'd like to add
- Multiple channel support
- Private messages
- Logging support
- Encryption
