//
// Created by paul on 8/15/22.
//

#ifndef UCIFS_UCI2LIBELEKTRA_H
#define UCIFS_UCI2LIBELEKTRA_H

typedef unsigned char byte;
typedef unsigned long tHash;

/* these are opaque */
typedef struct sMountPoint tMountPoint;
typedef struct sFileHandle tFileHandle;

tMountPoint *   getMountPoint( void );

int isDirectory( const char * path );
int getFileAttributes( tFileHandle * fh, struct stat * st );
int getDirAttributes( tMountPoint * mountPoint, struct stat * st );

tMountPoint *   initRoot(     uid_t uid, gid_t gid );
int             populateRoot( tMountPoint * mountPoint );
int             releaseRoot(  tMountPoint * mountPoint );

tFileHandle *   newFH(      const char * path, int mode );
tFileHandle *   findFH(     const char * path );
tFileHandle *   getFH(      tFileHandle * fh, const char * path );
tFileHandle *   nextFH(     tFileHandle * fh );
const char *    getFHpath(  tFileHandle * fh );
struct stat *   getFHstat(  tFileHandle * fh );
ssize_t         readFH(     tFileHandle * fh, char *buffer, size_t size, off_t offset );
ssize_t         writeFH(    tFileHandle * fh, const char *buffer, size_t size, off_t offset );
int             truncateFH( tFileHandle * fh, off_t offset );
int             populateFH( tFileHandle * fh );
int             parseFH(    tFileHandle * fh );
void            releaseFH(  tFileHandle * fh );

#endif //UCIFS_UCI2LIBELEKTRA_H
