#include <stdio.h>

#include "Receiver.h"

#define SUCCESS_CODE 0

int main(int argc, char *argv[]) {
	if (argc != 3) {
		fprintf(stderr, "Not the right amount of input arguments.\nNeed to give two.\nExiting...\n"); // first is path, other two are inputs
		return ERROR_CODE;
	}
	InitReceiver(argv);
	BindToPort();
	HandleReceiver();
	CloseSocketsThreadsAndWsaData();
	return SUCCESS_CODE;
}