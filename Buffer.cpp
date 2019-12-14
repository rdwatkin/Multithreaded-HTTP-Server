#include "Buffer.h"

int32_t KiB = 1024;
	
/* Constructor */
Buffer::Buffer( int32_t FD ) {
	sockFD = FD;
	data = (int8_t*)malloc(10*KiB);
	validData = 0;
}

/* Desctructor */
Buffer::~Buffer(){
	memset( data, 0, 10*KiB);
	free(data);
	close(sockFD);
}
	
/* Read Data */
int32_t Buffer::read_data( int64_t bytes ){
	int32_t freeSpace = 10*KiB - validData;
	if( bytes > freeSpace ){
			bytes = freeSpace;
	} 
	int32_t bytesRead = recv( sockFD, data+validData, bytes, 0 );
	validData += bytesRead;
	return bytesRead;
}
	
/* Get length of valid data in buffer */
int32_t Buffer::length(){
	return validData;
}

/* Get a read-only pointer to the data */
const int8_t* Buffer::peek(){
	const int8_t *ptr = data;
	return ptr;
}

/* Mark data as consumed */
int8_t Buffer::consume( int32_t bytes ){
	if( bytes > validData ){ return -1; }
	memset( data, 0, bytes );
	memmove( data, data+bytes, validData - bytes  );
	memset( data+validData-bytes, 0, bytes);
	validData -= bytes;
	return 0;
}

/* Get socket FD */
int32_t Buffer::getSocketFD(){
	return sockFD;
}