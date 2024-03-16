CC = gcc
CFLAGS = -Wall -Werror

all: 5g_auth_platform mobile_user

5g_auth_platform: SystemManager.c
	$(CC) $(CFLAGS) -o 5g_auth_platform SystemManager.c -lpthread

mobile_user: MobileUser.c
	$(CC) $(CFLAGS) -o mobile_user MobileUser.c

clean:
	rm -f 5g_auth_platform mobile_user
