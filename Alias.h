#include <sys/socket.h>
#include <string>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <iostream>

#ifndef ALIAS_H
#define ALIAS_H

struct Alias
{
    int32_t aliasFD;
	int32_t fileSize;
	int32_t hashTable[8000];
	
	/* Constructor and Destructor */
	Alias();
	Alias( const char* aliasFileName );
	~Alias();
	
	/* Hashtable Functions */
	int32_t get( const char* key );
	int8_t put( const char* key, int32_t value );
	bool has( std::string key );
	void remove( const char* key );
	
	/* Alias File Functions */
	int8_t addAlias( const char* alias, const char* fileName );
	std::string resolveAlias( std::string alias );
	
	char* readAlias( std::string alias );
	bool isValidFileName( std::string fileName );
};

#endif