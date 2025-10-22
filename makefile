default:
	${CC} main.c -o words -pthread -ldiscord -lcurl -lsqlite3 --std=gnu23
