CC = gcc
CFLAGS = -Wall -Werror

all: 5g_auth_platform

5g_auth_platform: SystemManager.c
	$(CC) $(CFLAGS) -o 5g_auth_platform SystemManager.c -lpthread

clean:
	rm -f 5g_auth_platform
