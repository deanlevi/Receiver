#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <stdlib.h>
#include <stdio.h>

#include "Receiver.h"

#define LOCAL_PORT_NUM_ARGUMENT_INDEX 1
#define OUTPUT_FILE_NAME_ARGUMENT_INDEX 2
#define SOCKET_PROTOCOL 0
#define BINDING_SUCCEEDED 0
#define SEND_RECEIVE_FLAGS 0
#define USER_INPUT_LENGTH 20
#define CHUNK_SIZE_IN_BYTES 8
#define NUM_OF_THREADS 2
#define TIME_OUT_FOR_SELECT 500000
#define NUM_OF_DATA_BITS 49
#define NUM_OF_ERROR_BITS 15
#define NUM_OF_DATA_BITS_IN_A_ROW_COLUMN 7
#define COLUMN_PARITY_OFFSET 7
#define DATA_TO_WRITE_IN_BYTES 7
#define DATA_TO_WRITE_IN_BITS 56
#define NUMBER_OF_BITS_IN_ONE_BYTE 8
#define MESSAGE_LENGTH 20

/*
Input: argv - to update input parameters.
Output: none.
Description: update receiver parameters and init variables.
*/
void InitReceiver(char *argv[]);

/*
Input: none.
Output: none.
Description: bind receiver socket to requested port.
*/
void BindToPort();

/*
Input: none.
Output: none.
Description: create threads for connection with channel and user interface.
*/
void HandleReceiver();

/*
Input: p_start_routine - a pointer to the function to be executed by the thread,
	   p_thread_id - a pointer to a variable that receives the thread identifier (output parameter),
	   p_thread_parameters - the argument to send to thread's function..
Output: if the function succeeds, the return value is a handle to the new thread, if not prints error and exits.
Description: creating the thread with the right parameters.
*/
HANDLE CreateThreadSimple(LPTHREAD_START_ROUTINE p_start_routine, LPVOID p_thread_parameters, LPDWORD p_thread_id);

/*
Input: none.
Output: none.
Description: handling receiver operation. receive data from channel, search and fix errors and write data to output file.
			 when receiving "End" from user, send relevant data to channel.
*/
void WINAPI ConnectionWithChannelThread();

/*
Input: none.
Output: none.
Description: handle user interface. when user enters "End" signaling to ConnectionWithChannelThread to finish operating.
*/
void WINAPI UserInterfaceThread();

/*
Input: none.
Output: none.
Description: create new ouput file (erase old if exists from previous runs).
*/
void CreateOutputFile();

/*
Input: ReceivedBuffer - received buffer of data from channel.
Output: none.
Description: implementing ECC by calculating parity of data block and xor it with received parity of data block. if result != 0 -> error.
			 see if only one line and one row is 'on' in result. if so -> can be fixed (flip corresponding bit), if not -> can't be fixed.
			 the calculation of the parity of data block is being done similarly to the senders calculation before sending the data.
*/
void FindAndFixError(unsigned long long *ReceivedBuffer);

/*
Input: ReceivedBuffer - received buffer of data from channel after running ECC (FindAndFixError).
Output: none.
Description: extract 49 bits of data from buffer, put data from each received buffer in chunks of 7 bytes and write to output file.
*/
void WriteInputToOutputFile(unsigned long long ReceivedBuffer);

/*
Input: none.
Output: none.
Description: receive data from channel, search and fix errors and write to output file.
*/
void HandleReceiveFromChannel();

/*
Input: none.
Output: none.
Description: after receiving "End" from user, send collected (and requested) information to channel.
*/
void SendInformationToChannel();

/*
Input: none.
Output: none.
Description: close sockets, thread handles and wsa data at the end of the receiver's operation.
*/
void CloseSocketsThreadsAndWsaData();

void InitReceiver(char *argv[]) {
	Receiver.LocalPortNum = atoi(argv[LOCAL_PORT_NUM_ARGUMENT_INDEX]);
	Receiver.OutputFileName = argv[OUTPUT_FILE_NAME_ARGUMENT_INDEX];
	Receiver.ConnectionWithChannelThreadHandle = NULL;
	Receiver.UserInterfaceThreadHandle = NULL;
	Receiver.GotEndFromUser = false;
	Receiver.NumberOfErrorsDetected = 0;
	Receiver.NumberOfErrorsCorrected = 0;
	Receiver.NumberOfReceivedBytes = 0;
	Receiver.NumberOfWrittenBits = 0;
	Receiver.NumberOfSpareDataBits = 0;
	Receiver.SpareDataBitsForNextChunk = 0;

	WSADATA wsaData;
	int StartupRes = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (StartupRes != NO_ERROR) {
		fprintf(stderr, "Error %ld at WSAStartup().\nExiting...\n", StartupRes);
		exit(ERROR_CODE);
	}

	Receiver.ReceiverSocket = socket(AF_INET, SOCK_DGRAM, SOCKET_PROTOCOL);
	if (Receiver.ReceiverSocket == INVALID_SOCKET) {
		fprintf(stderr, "InitReceiver failed to create socket. Error Number is %d\n", WSAGetLastError());
		CloseSocketsThreadsAndWsaData();
		exit(ERROR_CODE);
	}
}

void BindToPort() {
	int BindingReturnValue;
	Receiver.ReceiverSocketService.sin_family = AF_INET;
	Receiver.ReceiverSocketService.sin_addr.s_addr = INADDR_ANY;
	Receiver.ReceiverSocketService.sin_port = htons(Receiver.LocalPortNum);
	BindingReturnValue = bind(Receiver.ReceiverSocket, (SOCKADDR*)&Receiver.ReceiverSocketService,
		sizeof(Receiver.ReceiverSocketService));
	if (BindingReturnValue != BINDING_SUCCEEDED) {
		fprintf(stderr, "BindToPort failed to bind.\n");
		CloseSocketsThreadsAndWsaData();
		exit(ERROR_CODE);
	}
}

void HandleReceiver() {
	Receiver.ConnectionWithChannelThreadHandle = CreateThreadSimple((LPTHREAD_START_ROUTINE)ConnectionWithChannelThread,
																	 NULL,
																	&Receiver.ConnectionWithChannelThreadID);
	Receiver.UserInterfaceThreadHandle = CreateThreadSimple((LPTHREAD_START_ROUTINE)UserInterfaceThread,
															 NULL,
															&Receiver.UserInterfaceThreadID);
	HANDLE ThreadsArray[NUM_OF_THREADS] = { Receiver.ConnectionWithChannelThreadHandle, Receiver.UserInterfaceThreadHandle };
	DWORD wait_code;
	wait_code = WaitForMultipleObjects(NUM_OF_THREADS, ThreadsArray, TRUE, INFINITE);
	if (WAIT_OBJECT_0 != wait_code) {
		fprintf(stderr, "HandleReceiver failed to WaitForMultipleObjects.\n");
		CloseSocketsThreadsAndWsaData();
		exit(ERROR_CODE);
	}
}

HANDLE CreateThreadSimple(LPTHREAD_START_ROUTINE p_start_routine, LPVOID p_thread_parameters, LPDWORD p_thread_id) {
	HANDLE thread_handle;

	thread_handle = CreateThread(
		NULL,                /*  default security attributes */
		0,                   /*  use default stack size */
		p_start_routine,     /*  thread function */
		p_thread_parameters, /*  argument to thread function */
		0,                   /*  use default creation flags */
		p_thread_id);        /*  returns the thread identifier */

	if (NULL == thread_handle) {
		fprintf(stderr, "CreateThreadSimple failed to create thread.\n");
		CloseSocketsThreadsAndWsaData();
		exit(ERROR_CODE);
	}
	return thread_handle;
}

void WINAPI ConnectionWithChannelThread() {
	unsigned long long ReceivedBuffer;
	int FromLen = sizeof(Receiver.ChannelSocketService);
	int ReceivedBufferLength = recvfrom(Receiver.ReceiverSocket, &ReceivedBuffer, CHUNK_SIZE_IN_BYTES, SEND_RECEIVE_FLAGS,
									   (SOCKADDR*)&Receiver.ChannelSocketService, &FromLen);
	if (ReceivedBufferLength == SOCKET_ERROR || ReceivedBufferLength != CHUNK_SIZE_IN_BYTES) {
		fprintf(stderr, "ConnectionWithChannelThread failed to recvfrom. Error Number is %d\n", WSAGetLastError());
		CloseSocketsThreadsAndWsaData();
		exit(ERROR_CODE);
	}
	CreateOutputFile();
	Receiver.NumberOfReceivedBytes += ReceivedBufferLength;
	FindAndFixError(&ReceivedBuffer);
	WriteInputToOutputFile(ReceivedBuffer);
	struct timeval Tv;
	//Tv.tv_sec = 20; // todo check
	Tv.tv_usec = TIME_OUT_FOR_SELECT; // todo check what time to give
	fd_set Allfds;
	fd_set Readfds;
	int Status;
	FD_ZERO(&Allfds);
	FD_ZERO(&Readfds);
	FD_SET(Receiver.ReceiverSocket, &Allfds);

	while (!Receiver.GotEndFromUser) {
		Readfds = Allfds;
		Status = select(0, &Readfds, NULL, NULL, &Tv);
		if (Status == SOCKET_ERROR) {
			fprintf(stderr, "ConnectionWithChannelThread select failure. Error Number is %d\n", WSAGetLastError());
			CloseSocketsThreadsAndWsaData();
			exit(ERROR_CODE);
		}
		else if (Status == 0) {
			continue;
		}
		else {
			if (FD_ISSET(Receiver.ReceiverSocket, &Readfds)) {
				HandleReceiveFromChannel();
			}
		}
	}
	SendInformationToChannel();
	fprintf(stderr, "received: %d bytes\n", Receiver.NumberOfReceivedBytes);
	fprintf(stderr, "wrote: %d bytes\n", Receiver.NumberOfWrittenBits / NUMBER_OF_BITS_IN_ONE_BYTE);
	fprintf(stderr, "detected: %d errors, corrected: %d errors\n", Receiver.NumberOfErrorsDetected, Receiver.NumberOfErrorsCorrected);
}

void WINAPI UserInterfaceThread() {
	char UserInput[USER_INPUT_LENGTH];
	while (TRUE) {
		scanf("%s", UserInput);
		if (strcmp(UserInput, "End") == 0) {
			Receiver.GotEndFromUser = true;
			break;
		}
		else {
			fprintf(stderr, "Not a valid input. To finish enter 'End'.\n");
		}
	}
}

void CreateOutputFile() {
	FILE *OutputFilePointer = fopen(Receiver.OutputFileName, "w");
	if (OutputFilePointer == NULL) {
		fprintf(stderr, "CreateOutputFile couldn't open output file.\n");
		CloseSocketsThreadsAndWsaData();
		exit(ERROR_CODE);
	}
	fclose(OutputFilePointer);
}

void FindAndFixError(unsigned long long *ReceivedBuffer) {
	unsigned long long ReceivedErrorBits = *ReceivedBuffer >> NUM_OF_DATA_BITS;
	unsigned long long CalculatedErrorBits = 0, XoredErrorBits;
	unsigned long long DataBits = *ReceivedBuffer - (ReceivedErrorBits << NUM_OF_DATA_BITS);
	int RowParity = 0, ColumnParity = 0, DiagonalParity = 0, RowParityPositionInData, ColumnParityPositionInData,
		RowBit, ColumnBit;

	for (int RowIndexInData = 0; RowIndexInData < NUM_OF_DATA_BITS_IN_A_ROW_COLUMN; RowIndexInData++) {
		for (int ColumnIndexInData = 0; ColumnIndexInData < NUM_OF_DATA_BITS_IN_A_ROW_COLUMN; ColumnIndexInData++) {
			RowParityPositionInData = ColumnIndexInData + NUM_OF_DATA_BITS_IN_A_ROW_COLUMN * RowIndexInData;
			RowBit = (DataBits >> RowParityPositionInData) & 1;
			RowParity = RowParity ^ RowBit;
			ColumnParityPositionInData = RowIndexInData + NUM_OF_DATA_BITS_IN_A_ROW_COLUMN * ColumnIndexInData;
			ColumnBit = (DataBits >> ColumnParityPositionInData) & 1;
			ColumnParity = ColumnParity ^ ColumnBit;
		}
		CalculatedErrorBits = CalculatedErrorBits + (RowParity << RowIndexInData);
		CalculatedErrorBits = CalculatedErrorBits + (ColumnParity << (RowIndexInData + NUM_OF_DATA_BITS_IN_A_ROW_COLUMN));
		DiagonalParity = DiagonalParity ^ ColumnParity;
		RowParity = 0;
		ColumnParity = 0;
	}
	CalculatedErrorBits = CalculatedErrorBits + (DiagonalParity << (NUM_OF_ERROR_BITS - 1));
	XoredErrorBits = CalculatedErrorBits ^ ReceivedErrorBits;
	if (XoredErrorBits == 0) { // no error detected
		return;
	}
	Receiver.NumberOfErrorsDetected++;
	int NumberOfRowErrors = 0, NumberOfColumnErrors = 0, IndexOfRowError = 0, IndexOfColumnError = 0,
		CurrentRowErrorBit, CurrentColumnErrorBit;
	for (int IndexInXoredErrorBits = 0; IndexInXoredErrorBits < COLUMN_PARITY_OFFSET; IndexInXoredErrorBits++) {
		CurrentRowErrorBit = (XoredErrorBits >> IndexInXoredErrorBits) & 1;
		CurrentColumnErrorBit = (XoredErrorBits >> (IndexInXoredErrorBits + COLUMN_PARITY_OFFSET)) & 1;
		if (CurrentRowErrorBit != 0) {
			NumberOfRowErrors++;
			IndexOfRowError = IndexInXoredErrorBits;
		}
		if (CurrentColumnErrorBit != 0) {
			NumberOfColumnErrors++;
			IndexOfColumnError = IndexInXoredErrorBits;
		}
	}
	if (NumberOfRowErrors == 1 && NumberOfColumnErrors == 1) { // todo check condition
		int ErrorBitPosition = IndexOfRowError * COLUMN_PARITY_OFFSET + IndexOfColumnError;
		unsigned long long FixMask = 1 << ErrorBitPosition;
		DataBits = DataBits ^ FixMask;
		*ReceivedBuffer = DataBits; // no need to add error bits
		Receiver.NumberOfErrorsCorrected++;
	}
}

void WriteInputToOutputFile(unsigned long long ReceivedBuffer) {
	FILE *OutputFilePointer = fopen(Receiver.OutputFileName, "a");
	if (OutputFilePointer == NULL) {
		fprintf(stderr, "WriteInputToOutputFile couldn't open output file.\n");
		CloseSocketsThreadsAndWsaData();
		exit(ERROR_CODE);
	}
	int WroteElements;
	unsigned long long ErrorBits = (ReceivedBuffer >> NUM_OF_DATA_BITS);
	unsigned long long DataBits = ReceivedBuffer - (ErrorBits << NUM_OF_DATA_BITS);
	unsigned long long DataToWrite;

	if (Receiver.NumberOfSpareDataBits + NUM_OF_DATA_BITS < DATA_TO_WRITE_IN_BITS) {
		Receiver.NumberOfSpareDataBits += NUM_OF_DATA_BITS;
		Receiver.SpareDataBitsForNextChunk = DataBits;
		return; // not enough data to write
	}
	DataToWrite = Receiver.SpareDataBitsForNextChunk << (DATA_TO_WRITE_IN_BITS - Receiver.NumberOfSpareDataBits);
	Receiver.NumberOfSpareDataBits = Receiver.NumberOfSpareDataBits + NUM_OF_DATA_BITS - DATA_TO_WRITE_IN_BITS;
	DataToWrite += DataBits >> Receiver.NumberOfSpareDataBits;
	Receiver.SpareDataBitsForNextChunk = DataBits - ((DataBits >> Receiver.NumberOfSpareDataBits) << Receiver.NumberOfSpareDataBits);
	char *Temp = &DataToWrite;
	WroteElements = fwrite(&DataToWrite, DATA_TO_WRITE_IN_BYTES, 1, OutputFilePointer);
	if (WroteElements != 1) {
		fprintf(stderr, "WriteInputToOutputFile error in writing to file.\n");
		CloseSocketsThreadsAndWsaData();
		exit(ERROR_CODE);
	}
	fclose(OutputFilePointer);
	Receiver.NumberOfWrittenBits += NUM_OF_DATA_BITS;
}

void HandleReceiveFromChannel() {
	unsigned long long ReceivedBuffer;
	int ReceivedBufferLength = recvfrom(Receiver.ReceiverSocket, &ReceivedBuffer, CHUNK_SIZE_IN_BYTES, SEND_RECEIVE_FLAGS, NULL, NULL);
	if (ReceivedBufferLength == SOCKET_ERROR) {
		fprintf(stderr, "HandleReceiveFromChannel failed to recvfrom. Error Number is %d\n", WSAGetLastError());
		CloseSocketsThreadsAndWsaData();
		exit(ERROR_CODE);
	}
	Receiver.NumberOfReceivedBytes += ReceivedBufferLength;
	FindAndFixError(&ReceivedBuffer);
	WriteInputToOutputFile(ReceivedBuffer);
}

void SendInformationToChannel() {
	char MessageToChannel[MESSAGE_LENGTH];
	sprintf(MessageToChannel, "%d\n%d\n%d\n%d\n", Receiver.NumberOfReceivedBytes, Receiver.NumberOfWrittenBits / NUMBER_OF_BITS_IN_ONE_BYTE,
							   Receiver.NumberOfErrorsDetected, Receiver.NumberOfErrorsCorrected);
	int SentBufferLength = sendto(Receiver.ReceiverSocket, MessageToChannel, strlen(MessageToChannel), SEND_RECEIVE_FLAGS,
								 (SOCKADDR*)&Receiver.ChannelSocketService, sizeof(Receiver.ChannelSocketService));
	if (SentBufferLength == SOCKET_ERROR) {
		fprintf(stderr, "SendInformationToChannel failed to sendto. Error Number is %d\n", WSAGetLastError());
		CloseSocketsThreadsAndWsaData();
		exit(ERROR_CODE);
	}
}

void CloseSocketsThreadsAndWsaData() {
	int CloseSocketReturnValue;
	DWORD ret_val;
	CloseSocketReturnValue = closesocket(Receiver.ReceiverSocket);
	if (CloseSocketReturnValue == SOCKET_ERROR) {
		fprintf(stderr, "CloseSocketsThreadsAndWsaData failed to close socket. Error Number is %d\n", WSAGetLastError());
		exit(ERROR_CODE);
	}
	if (Receiver.ConnectionWithChannelThreadHandle != NULL) {
		ret_val = CloseHandle(Receiver.ConnectionWithChannelThreadHandle);
		if (FALSE == ret_val) {
			fprintf(stderr, "CloseSocketsThreadsAndWsaData failed to close ConnectionWithChannelThreadHandle.\n");
			exit(ERROR_CODE);
		}
	}
	if (Receiver.UserInterfaceThreadHandle != NULL) {
		ret_val = CloseHandle(Receiver.UserInterfaceThreadHandle);
		if (FALSE == ret_val) {
			fprintf(stderr, "CloseSocketsThreadsAndWsaData failed to close UserInterfaceThreadHandle.\n");
			exit(ERROR_CODE);
		}
	}
	if (WSACleanup() == SOCKET_ERROR) {
		fprintf(stderr, "CloseSocketsThreadsAndWsaData Failed to close Winsocket, error %ld. Ending program.\n", WSAGetLastError());
		exit(ERROR_CODE);
	}
}