default:
	${CC} main.c -o word-chain -pthread -ldiscord -lcurl -lsqlite3 --std=gnu23
