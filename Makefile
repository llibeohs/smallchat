all: smallchat-server smallchat-client
CFLAGS=-O2 -Wall -W -std=c99

smallchat-server: smallchat-server.c chatlib.c
	$(CC) smallchat-server.c chatlib.c -o smallchat-server $(CFLAGS)

smallchat-client: smallchat-client.c chatlib.c
	$(CC) smallchat-client.c chatlib.c -o smallchat-client $(CFLAGS)
	
# intel-app: CFLAGS += -arch x86_64
# intel-app: smallchat-server smallchat-client

# silicon-app: CFLAGS += -arch arm64
# silicon-app: smallchat-server smallchat-client

# linux-app: CC = x86_64-linux-gnu-gcc
# linux-app: CFLAGS += -static
# linux-app: smallchat-server smallchat-client

clean:
	rm -f smallchat-server
	rm -f smallchat-client
