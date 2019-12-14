/* Filename: httpserver.cpp
 * Author: Ryan Watkins */
 
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <err.h> 
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <map>
#include <queue>
#include <pthread.h>
#include <iostream>
#include <math.h>
#include "Buffer.h"
#include "Alias.h"

extern "C"
 
using namespace std;
const int32_t KiB = 1024;

 /* Functions */
int64_t initialize(char* host, char* port);
int8_t parse_header( Buffer* buf, const char* header );
int8_t handle_put( Buffer* buf, char* fileName, int contentLength, char* firstLine );
int8_t handle_get( Buffer* buf, char* fileName, char* firstLine );
int32_t read_content_length( const char* header );
int32_t get_content_length( char* fileName );
int8_t send_response( int sockFD, uint32_t responseNum, const char* responseName, int contentLength, char* request );
void declare_responses();
void free_responses();
char* get_header( Buffer* buf );
void* worker( void* threadID );
uint64_t calculate_offset( int32_t contentLength );
int8_t handle_patch( Buffer* buf, int32_t contentLength, char* firstLine );

/* Response Codes */
const char* ok_msg = (char*)malloc(sizeof(char) * 3 );
const char* created_msg = (char*)malloc(sizeof(char) * 8 );
const char* badrequest_msg = (char*)malloc(sizeof(char) * 12 );
const char* forbidden_msg = (char*)malloc(sizeof(char) * 10 );
const char* filenotfound_msg = (char*)malloc(sizeof(char) * 15 );
const char* servererror_msg = (char*)malloc(sizeof(char) * 21 );
std::map<const char*, uint32_t> responses;

/* Global Variables */
char* logFileName = (char*)"";
Alias* alias;

/* Synchronization Assets */
pthread_cond_t worker_cond;
pthread_cond_t dispatcher_cond;
pthread_mutex_t worker_mutex;
pthread_mutex_t dispatcher_mutex;
pthread_mutex_t writer_mutex;
pthread_mutex_t map_mutex;
queue<int32_t> jobs;
int32_t freeWorkers = 0;
uint64_t logOffset = 0;

void handleConnection( Buffer* buf ){
	buf->read_data(9*KiB);

	char *header = (char *)malloc(10 * KiB);
	while( buf->length() > 0 ){
		header = get_header( buf );
		if( header == NULL ){ 
				if( !(buf->read_data(9*KiB) > 0)){
					break;
				}
				continue;
		}

		parse_header( buf, header );
	}
	
	free( header );
}

int main( int argc, char *argv[] ){
	
	declare_responses();
	char defaultPort[10] = "80";
	int32_t mainSocket = 0;
	int32_t threadCount = 4;
	char* host = (char*)malloc(0.5*KiB);
	char* port = defaultPort;
	bool aFlagPresent = false;
	
	int opt;
	while ((opt = getopt(argc, argv, "N:l:a:")) != -1){
		switch (opt) {
			case 'N':
				if( optarg ){
					threadCount = atoi(optarg);
				}
				break;
			case 'l':
				logFileName = optarg;
				break;
			case 'a':
				alias = new Alias( optarg );
				aFlagPresent = true;
				break;
			default:
				break;
		}
	}
	
	if( ! aFlagPresent ){
		exit(1);
	}
	
	if( (argc-optind) != 2 ){
		//throw( "Usage: ./httpserver host port [ -N threads ] [ -l log ]\n " );
	}else{
		host = argv[optind];
		port = argv[optind+1];
	}
	
	mainSocket = initialize( host, port );
	if( mainSocket == -1 ){
		return 0;
	}
	
	int i = 0;
	pthread_t threads[100];
	for(; i<threadCount; ++i ){
		pthread_create(&threads[i], NULL, worker, NULL );
	}
	
	while(true){
		int32_t fd = accept( mainSocket, NULL, NULL );
		pthread_mutex_lock( &dispatcher_mutex );
			while( freeWorkers == 0 ){
				pthread_cond_wait( &dispatcher_cond, &dispatcher_mutex );
			}
			freeWorkers--;
			jobs.push( fd );
		pthread_mutex_unlock( &dispatcher_mutex );
		
		pthread_cond_signal( &worker_cond );
	}

	free_responses();
}

void* worker( void* ){
	while(true){
		pthread_mutex_lock( &worker_mutex );
		
		/* Update dispatcher */
		pthread_mutex_lock( &dispatcher_mutex );
			freeWorkers++;
		pthread_cond_signal( &dispatcher_cond );
		pthread_mutex_unlock( &dispatcher_mutex );
		
		/* Wait for Job */
		pthread_cond_wait( &worker_cond, &worker_mutex );
		pthread_mutex_lock( &dispatcher_mutex );
		Buffer* buf = new Buffer( jobs.front() );
		jobs.pop();
		pthread_mutex_unlock( &dispatcher_mutex );
		pthread_mutex_unlock( &worker_mutex );
		handleConnection( buf );
		delete buf;
	}	
	return NULL;
}

int8_t parse_header( Buffer* buf, const char* header ){

	char requestType[3];
	char fileName[128];
	char firstLine[60] = "";
	int32_t bytesRead = 0;
	char GET[] = "GET";
	char PUT[] = "PUT";
	char PATCH[] = "PATCH";
	
	for( size_t i = 0; i < strlen( header ); i++ ){
		if( header[i] == '\r' && header[i+1] == '\n' ){
			break;
		}
		firstLine[i] = header[i];
	}
	
	int8_t itemsRead = sscanf( header, "%s %s \r\n%n", requestType, fileName, &bytesRead);
	
	if( itemsRead != 2 ){
		send_response( buf->getSocketFD(), responses[badrequest_msg], badrequest_msg, 0, firstLine );
		return -1;
	}
	
	if( ! (strcmp( requestType, PUT ) == 0 ||
		    strcmp( requestType, GET ) == 0 ||
			strcmp( requestType, PATCH) == 0 ) ){
		send_response( buf->getSocketFD(), responses[badrequest_msg], badrequest_msg, 0, firstLine );
		return -1;
	}
	
	if( fileName[0] == '/' ){
		memmove( fileName, fileName+1, strlen(fileName) );
	}


	if( strcmp( requestType, GET ) == 0 ){
		char* file = (char*) malloc(128);
		pthread_mutex_lock( &map_mutex );
		string f = alias->resolveAlias( fileName );
		pthread_mutex_unlock( &map_mutex );
		strcpy( file, f.c_str() );
		f.clear();
		handle_get( buf, file, firstLine );
		free( file );
	}else if( strcmp( requestType, PUT ) == 0 ){
		char* file = (char*) malloc(128);
		pthread_mutex_lock( &map_mutex );
		string f = alias->resolveAlias( fileName );
		pthread_mutex_unlock( &map_mutex );
		strcpy( file, f.c_str() );
		f.clear();
		
		std::string name( file );
		if( name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos ){
			send_response( buf->getSocketFD(), responses[badrequest_msg], badrequest_msg, 0, firstLine );
			return -1;
		}

		const int16_t FILE_NAME_LENGTH = 27;
		if( name.length() != FILE_NAME_LENGTH ){
			send_response( buf->getSocketFD(), responses[badrequest_msg], badrequest_msg, 0, firstLine );
			return -1;
		}
		int contentLength = read_content_length( header );
		if( contentLength == -1 ){
			send_response( buf->getSocketFD(), responses[badrequest_msg], badrequest_msg, 0, firstLine );
			return -1;
		}
		handle_put( buf, file, contentLength, firstLine );
		free( file );
	}else if( strcmp( requestType, PATCH ) == 0 ){
		int contentLength = read_content_length( header );
		if( contentLength == -1 ){
			send_response( buf->getSocketFD(), responses[badrequest_msg], badrequest_msg, 0, firstLine );
			return -1;
		}
		
		handle_patch( buf, contentLength, firstLine );
	}
	
	return 1;
}

char* get_header( Buffer *buf ) {
	if(! (buf->length() > 4) ){
		return NULL;
	}
	
	const int8_t *ptr = buf->peek();
	char* input = (char*) malloc( 4 * KiB );
	
	for( int i=0; i <= buf->length()+4; ++i ){
		memcpy(input, ptr, 2 );
		if( strncmp( input, "\r\n", 2 ) == 0 ){
			ptr += 2;
			memcpy(input, ptr, 2);
			if( strncmp( input, "\r\n", 2 ) == 0 ){
				memcpy( input, buf->peek(), i+4 );
				buf->consume(i+4);
				return input;
			}
			ptr -= 2;
		} 
		++ptr;
	}
	return NULL;
}

int64_t initialize( char* host, char* inputPort ){
	char * hostname = host;
	char * port = inputPort;
	struct addrinfo *addrs, hints = {};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	
	if( getaddrinfo(hostname, port, &hints, &addrs) == -1 ){
		warn("");
		return -1;
	}
	
	int32_t main_socket = socket(addrs->ai_family, addrs->ai_socktype, addrs->ai_protocol);
	if (main_socket == -1){
		warn("");
		return -1;
	}
	int32_t enable = 1;
	
	if( setsockopt(main_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == -1 ){
		warn("");
		return -1;
	}
	
	if( bind(main_socket, addrs->ai_addr, addrs->ai_addrlen) == -1 ){ 
		warn(""); 
		return -1;
	}
	
	if( listen (main_socket, 16) == -1){
		warn("");
		return -1;
	}

	return main_socket;
}

int32_t read_content_length( const char* header ){
		if( header == NULL ){
			return -1;
		}
	char CONTENT_LENGTH[] = "Content-Length:";
	char scannedString[100];
	char fileName[29];
	int32_t bytesRead = 0;
	int32_t retVal = 0;
	
	bytesRead = sscanf( header, "%s %s \r\n%n", scannedString, fileName, &bytesRead);

	while( bytesRead != 0 ){
		if( strcmp( scannedString, CONTENT_LENGTH ) == 0 ){
			sscanf( header+bytesRead, "%d", &retVal );
			return retVal;
		}
		header += bytesRead;
		sscanf(header, "%s \r\n%n", scannedString, &bytesRead);
	}
	return -1;
}

int8_t handle_patch( Buffer* buf, int32_t contentLength, char* firstLine ){
	uint64_t offset;
	uint64_t logEntryLength;
	int32_t bytes = 0;
	int32_t logFD = 0;
	char* logData = (char*) malloc( 1*KiB );
	if( strcmp( logFileName, "" ) != 0 ){
		sprintf( firstLine, "%s %d\n", firstLine, contentLength );
		logEntryLength = calculate_offset( contentLength ) + strlen(firstLine);
		pthread_mutex_lock( &writer_mutex );
		offset = logOffset;
		logOffset += logEntryLength;
		pthread_mutex_unlock( &writer_mutex );
		logFD = open( logFileName, O_RDWR | O_CREAT, 0666 );
		pwrite( logFD, firstLine, strlen( firstLine ), offset );
		offset += strlen(firstLine);
	}
	
	char* data = (char*)malloc(200);
	char* key = (char*)malloc(200);
	char* fileName = (char*)malloc(200);
	while( contentLength > 0 ){
		while(buf->length() < contentLength && buf->length() < 4*KiB){
			buf->read_data(KiB);
		}
		if( buf->length() >= contentLength ){
			bytes = contentLength;
		}else{
			bytes = buf->length();
		}
		memcpy( data, buf->peek(), bytes );
		buf->consume(bytes);
		cerr << data;
		
		if( strcmp( logFileName, "" ) != 0 ){
			for( int i = 0; i < contentLength; i++ ){
				if( (i > 1) && (i % 20 == 0) ){
					pwrite( logFD, "\n", 1, offset );
					++offset;
				}
				if( i%20 == 0 ){
					sprintf( logData, "%08d ", i );
					pwrite( logFD, logData, 9, offset);
					offset += 9;
				}
				sprintf( logData, "%02x ", data[i] );
				pwrite( logFD, logData, 3, offset );
				offset += 3;
			}
			if( contentLength-1%20 != 0 ){
				pwrite( logFD, "\n", 1, offset );
				offset += 1;
			}
		}
		int16_t items_scanned = sscanf( data, "ALIAS %s %s %s\r\n", fileName, key, key );
		if( items_scanned != 2 ){
			send_response( buf->getSocketFD(), responses[badrequest_msg], badrequest_msg, 0, firstLine );
			return -1;
		}
		
		/* File Exists */
		char* file = (char*) malloc(128);
		pthread_mutex_lock( &map_mutex );
			string f = alias->resolveAlias( fileName );
		pthread_mutex_unlock( &map_mutex );
		strcpy( file, f.c_str() );
		f.clear();
		int32_t fd = open( file, O_RDWR );
		free( file );
		if( fd == -1 ){
			send_response( buf->getSocketFD(), responses[filenotfound_msg], filenotfound_msg, 0, firstLine );
			return -1;
		}
		
		for( size_t j=0; j<strlen(key); ++j){
			if( !isgraph( key[j] ) ){
					send_response( buf->getSocketFD(), responses[badrequest_msg], badrequest_msg, 0, firstLine );
					return -1;
			}
		}
		
		if( strlen(key) + strlen(fileName) + 2 > 128 ){
			send_response( buf->getSocketFD(), responses[badrequest_msg], badrequest_msg, 0, firstLine );
			return -1;
		}
		
		pthread_mutex_lock( &map_mutex );
			alias->addAlias( key, fileName );
		pthread_mutex_unlock( &map_mutex );
		contentLength -= bytes;
	}
	if( strcmp( logFileName, "" ) != 0 ){
		pwrite( logFD, "========\n", 9, offset );
		offset += 9;
	}
	send_response( buf->getSocketFD(), responses[ok_msg], ok_msg, 0, (char*)"" );
	free( logData );
	return 1;
}
	
int8_t handle_put( Buffer* buf, char* fileName, int32_t contentLength, char* firstLine ){
	cerr << "PU " << fileName << endl;
	char *data = (char *)malloc(4 * KiB);
	int32_t bytes = 0;
	int32_t fd = open( fileName, O_RDWR | O_CREAT, 0666 );
	int32_t logFD = 0;
	if( fd == -1 ){
		send_response( buf->getSocketFD(), responses[forbidden_msg], forbidden_msg, 0, firstLine );
		return -1;
	}
	
	uint64_t offset;
	uint64_t logEntryLength;
	char* logData = (char*) malloc( 1*KiB );
	if( strcmp( logFileName, "" ) != 0 ){
		char* testString = (char*) malloc( 0.5 * KiB );
		sprintf( testString, "PUT %s %d\n", fileName, contentLength );
		logEntryLength = calculate_offset( contentLength ) + strlen(testString);
		pthread_mutex_lock( &writer_mutex );
		offset = logOffset;
		logOffset += logEntryLength;
		pthread_mutex_unlock( &writer_mutex );
		logFD = open( logFileName, O_RDWR | O_CREAT, 0666 );
		pwrite( logFD, testString, strlen( testString )+1, offset );
		offset += strlen(testString)+1;
	}

	while( contentLength > 0 ){
		while(buf->length() < contentLength && buf->length() < 4*KiB){
			buf->read_data(KiB);
		}
		if( buf->length() >= contentLength ){
			bytes = contentLength;
		}else{
			bytes = buf->length();
		}
		memcpy( data, buf->peek(), bytes );
		buf->consume(bytes);
		
		if( strcmp( logFileName, "" ) != 0 ){
			for( int i = 0; i < contentLength; i++ ){
				if( (i > 1) && (i % 20 == 0) ){
					pwrite( logFD, "\n", 1, offset );
					++offset;
				}
				if( i%20 == 0 ){
					sprintf( logData, "%08d ", i );
					pwrite( logFD, logData, 9, offset);
					offset += 9;
				}
				sprintf( logData, "%02x ", data[i] );
				pwrite( logFD, logData, 3, offset );
				offset += 3;
			}
			if( contentLength-1%20 != 0 ){
				pwrite( logFD, "\n", 1, offset );
				offset += 1;
			}
		}
		
		write( fd, data, bytes );
		contentLength -= bytes;
	}
	if( strcmp( logFileName, "" ) != 0 ){
		pwrite( logFD, "========\n", 9, offset );
		offset += 9;
	}
	send_response( buf->getSocketFD(), responses[created_msg], created_msg, 0, (char*)"" );

	free( logData );
	free( data );
	close(fd);
	return 1;
}

int8_t handle_get( Buffer* buf, char* fileName, char* firstLine ){
		cerr << "G " << fileName << endl;
	std::string name( fileName );
	if( name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos ){
		send_response( buf->getSocketFD(), responses[badrequest_msg], badrequest_msg, 0, firstLine );
		return -1;
	}

	const int16_t FILE_NAME_LENGTH = 27;
	if( name.length() != FILE_NAME_LENGTH ){
		send_response( buf->getSocketFD(), responses[badrequest_msg], badrequest_msg, 0, firstLine );
		return -1;
	}
	
	int32_t fd = open( fileName, O_RDWR );
	int32_t logFD = 0;
	
	if( fd == -1 ){
		send_response( buf->getSocketFD(), responses[filenotfound_msg], filenotfound_msg, 0, firstLine );
		return -1;
	}
	
	int32_t contentLength = get_content_length( fileName );
	
	if( contentLength == -1 ){
		send_response( buf->getSocketFD(), responses[servererror_msg], servererror_msg, 0, firstLine );
		return -1;
	}
	
	uint64_t offset;
	uint64_t logEntryLength;
	if( strcmp( logFileName, "" ) != 0 ){
		char* testString = (char*) malloc( 0.5 * KiB );
		sprintf( testString, "GET %s %d\n", fileName, contentLength );
		logEntryLength = calculate_offset( contentLength ) + strlen(testString);
		pthread_mutex_lock( &writer_mutex );
		offset = logOffset;
		logOffset += logEntryLength;
		pthread_mutex_unlock( &writer_mutex );
		logFD = open( logFileName, O_RDWR | O_CREAT, 0666 );
		pwrite( logFD, testString, strlen( testString )+1, offset );
		offset += strlen(testString)+1;
	}
	
	int32_t bytesRead = 0;
	uint8_t* data = (uint8_t*)malloc(4*KiB);
	char* logData = (char*) malloc( 1*KiB );
	send_response( buf->getSocketFD(), responses[ok_msg], ok_msg, contentLength, (char*)"");
	while(contentLength > 0) {
		bytesRead = read( fd, data, KiB);
		if( bytesRead == -1 ){
			send_response( buf->getSocketFD() , responses[forbidden_msg], forbidden_msg, 0, firstLine );
			return -1;
		}
		
		if( strcmp( logFileName, "" ) != 0 ){
			for( int i = 0; i < contentLength; i++ ){
				if( (i > 1) && (i % 20 == 0) ){
					pwrite( logFD, "\n", 1, offset );
					++offset;
				}
				if( i%20 == 0 ){
					sprintf( logData, "%08d ", i );
					pwrite( logFD, logData, 9, offset);
					offset += 9;
				}
				sprintf( logData, "%02x ", data[i] );
				pwrite( logFD, logData, 3, offset );
				offset += 3;
			}
			if( contentLength-1%20 != 0 ){
				pwrite( logFD, "\n", 1, offset );
				offset += 1;
			}
		}
		write( buf->getSocketFD(), data, bytesRead);
		contentLength -= bytesRead;
	}
	if( strcmp( logFileName, "" ) != 0 ){
		pwrite( logFD, "========\n", 9, offset );
		offset += 9;
	}

	free( logData );
	free( data );
	close(fd);
	return 1;
}

uint64_t calculate_offset( int32_t contentLength ){
	uint64_t retval = (10 * ceil( contentLength/20 ) );
	retval += contentLength * 3;
	return retval+2+18;
}

int32_t get_content_length( char* fileName ){
	if( fileName == NULL ){
			return -1;
	}
	
	struct stat stats;
	if(stat(fileName, &stats) == -1){
		return -1;
	}
	int64_t length = stats.st_size;
	return length;
}

int8_t send_response( int sockFD, uint32_t responseNum, const char* responseName, int32_t contentLength, char* request ){
	char* response = (char*)malloc(1024);
	sprintf(response, "HTTP/1.1 %d %s\r\nContent-Length: %d\r\n\r\n",
				responseNum, responseName, contentLength );
	send( sockFD, response, strlen(response), 0 );
	
	if( strcmp( logFileName, "" ) != 0 && responseNum != 200 && responseNum != 201 ){
		sprintf( response, "%s --- response %d\n", request, responseNum );
		pthread_mutex_lock( &writer_mutex );
		int64_t offset = logOffset;
		logOffset += strlen(response);
		pthread_mutex_unlock( &writer_mutex );
		int32_t logFD = open( logFileName, O_RDWR | O_CREAT, 0666 );
		pwrite( logFD, response, strlen(response), offset );
	}
	return 1;
}

void declare_responses(){
	ok_msg = "OK";
	created_msg = "Created";
	badrequest_msg = "Bad Request";
	forbidden_msg = "Forbidden";
	filenotfound_msg = "File Not Found";
	servererror_msg = "Internal Server Error";
	
	responses[ok_msg] = 200;
	responses[created_msg] = 201;
	responses[badrequest_msg] = 400;
	responses[forbidden_msg] = 403;
	responses[filenotfound_msg] = 404;
	responses[servererror_msg] = 500;
}

void free_responses(){
	free(&ok_msg);
	free(&created_msg);
	free(&badrequest_msg);
	free(&forbidden_msg);
	free(&filenotfound_msg);
	free(&servererror_msg);
}















