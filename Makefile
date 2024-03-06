#CCFLAGS = -O2 -Wall -Werror
CCFLAGS = -O0 -g -Wall -Werror

all: lotto_client lotto_server
clean:
	rm lotto_client lotto_server

lotto_client: lotto_client.c
	cc $(CCFLAGS) -o lotto_client lotto_client.c
lotto_server: lotto_server.c
	cc $(CCFLAGS) -o lotto_server lotto_server.c -pthread 
