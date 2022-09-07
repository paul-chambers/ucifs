//
// Created by paul on 8/31/22.
//

#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "utils.h"
#include "logStuff.h"

/**
 * @brief
 * @param string
 * @return
 */
tHash hashString( const char * string )
{
    tHash hash = 0xDeadBeef;
    byte c;

    while ((c = *string++) != '\0' )
    {
        hash = (hash * 43) ^ c;
    }

    return hash;
}

/**
 * @brief
 * @param keyName
 * @param append
 * @return
 */
char * appendKeyName( char * keyName, const char * append )
{
    size_t len = strlen( append ) + 1;
    if ( keyName == NULL )
    {
        keyName = malloc( len );
        keyName[0] = '/';
        memcpy( &keyName[1], append, len );
        keyName[len + 1] = '\0';
    }
    else
    {
        size_t stemLen = strlen( keyName );
        keyName = realloc( keyName, stemLen + len + 1 + 1 );
        keyName[stemLen++] = '/';
        memcpy( &keyName[stemLen], append, len );
        keyName[stemLen + len ] = '\0';
    }
    return keyName;
}

/**
 * @brief
 * @param keyName
 * @return
 */
char * trimKey( char * keyName )
{
    char * lastSlash = strrchr( keyName, '/' );
    if ( lastSlash != NULL )
    {
        *lastSlash = '\0';
    }
    return keyName;
}

/**
 * @brief
 * @param keyName
 * @param newSpace
 * @return
 */
char * replaceKeySpace( const char * keyName, const char * newSpace )
{
    char * result;

    const char * p = strchr( keyName, '/');
    if ( p == NULL )
    {
        p = keyName;
    }
    size_t keyLen   = strlen( p );
    size_t spaceLen = strlen( newSpace );
    size_t newLen   = spaceLen + keyLen + sizeof(':') + sizeof('\0');
    result = calloc( 1, newLen );
    strncpy( result, newSpace, newLen );
    result[ spaceLen++ ] = ':';
    strncpy( &result[ spaceLen ], p, newLen - spaceLen );

    return result;
}

/**
 * @brief
 * @param p
 * @param remaining
 * @param flagName
 * @return
 */
char * appendStr( char * p, long remaining, const char * flagName )
{
    const char * src = flagName;
    *p++ = ',';
    --remaining;

    while ( *src != '\0' && remaining > 1)
    {
        *p++ = *src++;
        --remaining;
    }
    *p = '\0';

    return p;
}

/**
 * @brief
 * @param flags
 * @return
 */
const char * openFlagsAsStr( int flags )
{
    static char temp[1024];
    temp[0] = '\0';
    temp[1] = '\0';
    char * p = temp;
    char * end = &temp[sizeof(temp)];

    switch ( flags & O_ACCMODE )
    {
    case O_RDONLY: p = appendStr( p, end - p, "RdOnly" ); break;
    case O_WRONLY: p = appendStr( p, end - p, "WrOnly" ); break;
    case O_RDWR:   p = appendStr( p, end - p, "RdWr"   ); break;
    }
    if ( flags & O_CREAT     /* 0x000040 */ )  p = appendStr( p, end - p, "Create" );
    if ( flags & O_EXCL      /* 0x000080 */ )  p = appendStr( p, end - p, "Excl" );
    if ( flags & O_NOCTTY    /* 0x000100 */ )  p = appendStr( p, end - p, "NoCTTY" );
    if ( flags & O_TRUNC     /* 0x000200 */ )  p = appendStr( p, end - p, "Trunc" );
    if ( flags & O_APPEND    /* 0x000400 */ )  p = appendStr( p, end - p, "Append" );
    if ( flags & O_NONBLOCK  /* 0x000800 */ )  p = appendStr( p, end - p, "NonBlock" );
    if ( flags & O_DSYNC     /* 0x001000 */ )  p = appendStr( p, end - p, "DSync" );
    if ( flags & O_ASYNC     /* 0x002000 */ )  p = appendStr( p, end - p, "ASync" );
    if ( flags & O_DIRECT    /* 0x004000 */ )  p = appendStr( p, end - p, "Direct" );
    if ( flags & O_LARGEFILE /* 0x008000 */ )  p = appendStr( p, end - p, "LargeFile" );
    if ( flags & O_DIRECTORY /* 0x010000 */ )  p = appendStr( p, end - p, "Directory" );
    if ( flags & O_NOFOLLOW  /* 0x020000 */ )  p = appendStr( p, end - p, "NoFollow" );
    if ( flags & O_NOATIME   /* 0x040000 */ )  p = appendStr( p, end - p, "NoAtime" );
    if ( flags & O_CLOEXEC   /* 0x080000 */ )  p = appendStr( p, end - p, "CloExec" );
    if ( flags & O_PATH      /* 0x200000 */ )  p = appendStr( p, end - p, "Path" );
    if ( flags & O_TMPFILE   /* 0x400000 */ )  p = appendStr( p, end - p, "TmpFile" );

    return &temp[1];
}

/**
 * @brief
 * @param mode
 * @return
 */
const char * createModeAsStr( unsigned int mode )
{
    static char temp[10];

    memset( temp, '-', 9 );
    temp[9] = '\0';

    if ( mode & S_IRUSR ) temp[0] = 'r';
    if ( mode & S_IWUSR ) temp[1] = 'w';
    if ( mode & S_IXUSR ) temp[2] = 'x';
    if ( mode & S_IRGRP ) temp[3] = 'r';
    if ( mode & S_IWGRP ) temp[4] = 'w';
    if ( mode & S_IXGRP ) temp[5] = 'x';
    if ( mode & S_IROTH ) temp[6] = 'r';
    if ( mode & S_IWOTH ) temp[7] = 'w';
    if ( mode & S_IXOTH ) temp[8] = 'x';

    return temp;
}
