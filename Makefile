
CC = cc
PROGRAME = memcache-cli
SRC = memcache-cli.c

build: $(DEPS)
	$(CC) -std=c99 -Wall -ledit -g -o $(PROGRAME) $(SRC)
	ls
clean:
	rm -rf *.o $(PROGRAME)
	ls
