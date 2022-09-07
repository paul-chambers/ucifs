//
// Created by paul on 8/31/22.
//

#ifndef UCIFS_UTILS_H
#define UCIFS_UTILS_H

typedef unsigned char byte;
typedef unsigned long tHash;

tHash hashString( const char * string );
char * appendKeyName( char * keyName, const char * append );
char * trimKey( char * keyName );
char * replaceKeySpace( const char * keyName, const char * newSpace );

const char * openFlagsAsStr( int flags );
const char * createModeAsStr( unsigned int mode );

#endif //UCIFS_UTILS_H
