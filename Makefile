# Compilateur et options
CC = gcc
CFLAGS = -Wall -Wextra -g
# Bibliothèques (SQLite3, Threads, Real-time)
LIBS_DAEMON = -lsqlite3 -lpthread -lrt
LIBS_CLIENT = -lpthread -lrt

all: daemon client

# Compilation du Daemon
daemon: daemon.o database.o
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_DAEMON)

# Compilation du Client
client: client.o
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_CLIENT)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
clean:
	rm -f *.o daemon client

.PHONY: all clean
