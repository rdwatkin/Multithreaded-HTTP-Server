/* Author: Ryan Watkins
 * Date: 12:5:2019 */

#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#ifndef Buffer_H
#define Buffer_H

struct Buffer {
	int32_t sockFD;
	int8_t *data;
	int32_t validData;
	
	/* Constructor */
	Buffer( int FD );
	
	/* Desctructor */
	~Buffer();
	
	/* Read Data */
	int32_t read_data( int64_t bytes );
	
	/* Get length of valid data in buffer */
	int32_t length();
	
	/* Get a read-only pointer to the data */
	const int8_t* peek();
	
	/* Mark data as consumed */
	int8_t consume( int32_t bytes );
	
	/* Get socket FD */
	int32_t getSocketFD();
		
};

#endif