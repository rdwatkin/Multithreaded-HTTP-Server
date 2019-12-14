#include "Alias.h"

#define SIZE 8000
#define MAX_ALIAS_LENGTH 128

std::hash<std::string> hasher;

/* Default Constructor */
Alias::Alias(){
	aliasFD = -1;
	fileSize = -1;
}

/* Constructor */
Alias::Alias(const char* aliasFileName ){
	aliasFD = open( aliasFileName, O_RDWR | O_CREAT, 0666 );
	
	for( size_t i = 0; i<SIZE; ++i ){
		hashTable[i] = -1;
	}
	
	struct stat stats;
	if(stat(aliasFileName, &stats) == 0){
		for(int i=0; i<stats.st_size; i+=128 ){
			char* entry = (char*)malloc( MAX_ALIAS_LENGTH );
			char* key = (char*)malloc( MAX_ALIAS_LENGTH );
			pread( aliasFD, entry, 128, i );
			sscanf( entry, "%s ", key );
			Alias::put( key, i );
		}
	}
	
	fileSize = 0;
}

/* Destructor */
Alias::~Alias(){
	close( aliasFD );
}

/*  =============== Hashtable Functions =============== */
	int32_t Alias::get( const char* key ){
		std::string myKey(key);
		if( ! Alias::has( myKey ) ){ return -1; }
		size_t index = hasher(myKey);
		return hashTable[index%SIZE];
	}

	/* Return 0 on success and -1 on collision or overwrite */
	int8_t Alias::put( const char* key, int32_t value ){
		int8_t retVal = 0;
		std::string myKey(key);
		size_t index = hasher(myKey);
		std::cout << "PUT: key = " << key << " HASH = " << index%8000 << std::endl;
		if( hashTable[index%SIZE] != -1 ){ retVal = -1; }
		hashTable[index%SIZE] = value;
		return retVal;
	}

	bool Alias::has( std::string key ){
		size_t index = hasher(key);
		//std::cout <<"KEY = " << key << " HASH = " << index%8000 << std::endl;
		//std::cout << "Value = " << hashTable[index%SIZE] << std::endl;
		if( hashTable[index%SIZE] == -1 ){ return false; }
		return true;
	}

	void Alias::remove( const char* key ){
/* 		char* myKey = (char*)malloc( MAX_ALIAS_LENGTH );
		memcpy( myKey, key, strlen(key) );
		uint32_t index = CityHash32( myKey, MAX_ALIAS_LENGTH );
		hashTable[ index%SIZE ] = -1; */
		std::string myKey(key);
		hashTable[ hasher(myKey)%SIZE ] = -1;
	}

/*  =============== Alias File Functions =============== */
int8_t Alias::addAlias( const char* alias, const char* fileName ){
	char* entry = (char*) malloc( MAX_ALIAS_LENGTH );
	char* tmp = (char*) malloc( 128 );
	sprintf( entry, "%s %s\n", alias, fileName );
	sscanf( entry, "%s", tmp );
	std::cout << "Add Alias = " << tmp << std::endl;
	if( has( tmp ) ){
		pwrite( aliasFD, entry, 128, get( tmp ) );
		return 0;
	}else{
		pwrite( aliasFD, entry, 128, fileSize );
		put( tmp, fileSize );
		fileSize += MAX_ALIAS_LENGTH;
		return 0;
	}
	return -1;
}

std::string Alias::resolveAlias( std::string alias ){
	std::string fileName = alias+"\0";
	for( int i = 0; i<100; ++i ){
			if( isValidFileName( fileName ) ){
				return fileName; 
			}
			if( !Alias::has( fileName ) ){ 
				return fileName; 
			 }
			
			fileName = readAlias( fileName );
			
	}
	return fileName;
}

char* Alias::readAlias( std::string alias ){
	std::cout << "Read Alias = " << alias << std::endl;
	char* space = (char*)malloc(1*sizeof(char));
	space[0] = ' ';
	if( !Alias::has( alias ) ) { return space; }
	int32_t offset = get( alias.c_str() );
	char* entry = (char*) malloc( 128 );
	char* fileName = (char*) malloc( 128 );
	char* key = (char*) malloc(128);
	pread( aliasFD, entry, 128, offset );
	sscanf( entry, "%s %s\n", key, fileName );
	return fileName;
}

bool Alias::isValidFileName( std::string fileName ){

	std::string name( fileName );
	if( name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos ){
		return false;
	}

	const int16_t FILE_NAME_LENGTH = 27;
	if( name.length() != FILE_NAME_LENGTH ){
		return false;
	}
	
	return true;
}






