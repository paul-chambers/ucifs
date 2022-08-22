//
// Created by paul on 8/15/22.
//

#define _GNU_SOURCE

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

#include <string.h>
#include <time.h>

#include <stdio.h>
#include <sys/stat.h>

/* need to build and install the uci project on x86 */
#include <uci.h>

#include <elektra.h>

#include "uci2libelektra.h"
#include "logStuff.h"

typedef struct sFileHandle {
    tFileHandle *       next;
    struct stat         st;
    const char *        path;
    tHash               pathHash;
    char *              contents;
    int                 buildCount;     // for 'mark/sweep' GC. will be set equal to buildCounter during a populateRoot.
    tBool               dirty;
} tFileHandle;

typedef struct sMountPoint {
    time_t               lastUpdated;    // last time the root dir was populated
    int                  buildCounter;   // part of a 'mark/sweep' algo to maintain the root dir
    struct sFileHandle * rootFiles;
    struct stat          rootStat;
} tMountPoint;

typedef struct sSection {
    struct sSection *   next;
    tHash               hash;
    int                 count;
    int                 counter;
} tSection;

#ifdef DEBUG
const char * testPaths[] = { "/network", "/system", "/wireless", "/dhcp", NULL };
#endif

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

char * appendKey( char * key, char * append )
{
    size_t len = strlen( append ) + 1;
    if ( key == NULL )
    {
        key = malloc( len );
        key[0] = '/';
        memcpy( &key[1], append, len );
        key[len + 1] = '\0';
    }
    else
    {
        size_t stemLen = strlen( key );
        key = realloc( key, stemLen + len + 1 + 1 );
        key[stemLen++] = '/';
        memcpy( &key[stemLen], append, len );
        key[stemLen + len ] = '\0';
    }
    return key;
}

char * trimKey( char * key )
{
    char * lastSlash = strrchr( key, '/' );
    if ( lastSlash != NULL )
    {
        *lastSlash = '\0';
    }
    return key;
}

tFileHandle * newFH( const char * path, int mode )
{
    tFileHandle * result = NULL;

    if ( path != NULL && *path != '\0' )
    {
        logDebug( "new fh for \'%s\'", path );

        result = calloc( 1, sizeof( tFileHandle ) );
        if ( result != NULL )
        {
            result->path     = strdup( path );
            result->pathHash = hashString( result->path );

            // GNU's definitions of the attributes (http://www.gnu.org/software/libc/manual/html_node/Attribute-Meanings.html):
            //  st_uid:    The user ID of the file’s owner.
            //	st_gid:    The group ID of the file.
            //	st_atime:  This is the last access time for the file.
            //	st_mtime:  This is the time of the last modification to the contents of the file.
            //	st_mode:   Specifies the mode of the file. This includes file type information (see Testing File Type)
            //             and the file permission bits (see Permission Bits).
            //	st_nlink:  The number of hard links to the file. This count keeps track of how many directories have
            //             entries for this file. If the count is ever decremented to zero, then the file itself is
            //             discarded as soon as no process still holds it open. Symbolic links are not counted in the
            //             total.
            //	st_size:   This specifies the size of a regular file in bytes. For files that are really devices this
            //             field isn’t usually meaningful. For symbolic links this specifies the length of the file
            //             name the link refers to.

            if ( mode == 0 )
            {
                mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH; // 0644
            }
            result->st.st_mode  = mode;
            result->st.st_nlink = 1;
            result->st.st_size  = 1024;

            time_t now = time(NULL);
            result->st.st_atime = now; // The last "a"ccess of the file
            result->st.st_mtime = now; // The last "m"odification of the file
            result->st.st_ctime = now; // The last "c"hange of the attributes of the file (it's new)

            tMountPoint * mountPoint = getMountPoint();
            if ( mountPoint != NULL )
            {
                result->st.st_uid = mountPoint->rootStat.st_uid;
                result->st.st_gid = mountPoint->rootStat.st_gid;

                mountPoint->rootStat.st_mtime = now; // we have "m"odified the root directory
                mountPoint->rootStat.st_ctime = now; // also "c"hanged the attributes of the root directory

                /* add it to the current list of files in the root dir */
                result->next = mountPoint->rootFiles;
                mountPoint->rootFiles = result;
            }
        }
    }

    return result;
}

const char * iterateUCIfiles( int i )
{
    const char * path = testPaths[i];
    logDebug( "%d: test path = \'%s\'", i, path );
    return path;
}

tFileHandle * findFH( const char * path )
{
    tFileHandle * result = NULL;

    tMountPoint * mountPoint = getMountPoint();
    if ( mountPoint != NULL)
    {
        /* make sure the root cache is populated & up-to-date */
        populateRoot( mountPoint );

        tHash hash = hashString( path );
        for ( result = mountPoint->rootFiles; result != NULL; result = result->next )
        {
            if ( hash == result->pathHash )
            {
                /* found an existing matching entry, so exit loop prematurely */
                break;
            }
        }
    }
    return result;
}

tFileHandle * getFH( tFileHandle * fh, const char * path )
{
    if ( path == NULL || *path == '\0' )
    {
        logError( "supplied path is empty" );
        return NULL;
    }

    /* if fh is NULL, search through the root files for a
     * match to the path. Often the case for doGetAttr() */
    if ( fh == NULL )
    {
        fh = findFH( path );
    }

#ifdef DEBUG
    if ( fh == NULL)
    {
        logError("no matching UCI file handle found");
    }
    else if ( fh->path == NULL)
    {
        logError( "fh->path is null" );
        free( fh );
        fh = NULL;
    }
    else if ( strcmp( fh->path, path ) != 0 )
    {
        logError( "path and fh->path do not match" );
        free( (void *)fh->path );
        free( fh );
        fh = NULL;
    }
#endif

    return fh;
}


tFileHandle * nextFH( tFileHandle * fh )
{
    if ( fh != NULL )
    {
        fh = fh->next;
    }
    else
    {
        tMountPoint * mountPoint = getMountPoint();
        fh = mountPoint->rootFiles;
    }
    return fh;
}


const char * getFHpath( tFileHandle * fh )
{
    return fh->path;
}


struct stat * getFHstat( tFileHandle * fh )
{
    return &fh->st;
}


ssize_t readFH( tFileHandle * fh, char *buffer, size_t size, off_t offset)
{
    ssize_t remaining = fh->st.st_size - offset;
    if ( remaining < 0 )
        remaining = 0;

    ssize_t length = (ssize_t)size;
    // do we have enough data to satisfy the whole request?
    if ( length > remaining )
    {
        // no, so trim it down to what remains
        length = remaining;
    }

    if ( length > 0 )
    {
        memcpy( buffer, &fh->contents[offset], length );
    }
    else {
        length = -1; // no more data to read - 'end of file'
    }

    return length;
}


ssize_t writeFH( tFileHandle * fh, const char *buffer, size_t size, off_t offset )
{
    ssize_t end = (ssize_t)(offset + size);
    char * contents = fh->contents;
    if ( contents == NULL )
    {
        logDebug( "  calloc %ld bytes", end );
        contents = calloc( end, sizeof(byte) );
        fh->st.st_size = end;
    }
    else if ( end > fh->st.st_size)
    {
        logDebug( "  realloc to %ld bytes", end );
        contents = realloc( contents, end );
        fh->st.st_size = end;
    }

    if ( contents == NULL )
    {
        logError( "failed to allocate memory for write");
        size = 0;
    }
    else
    {
        fh->contents = contents; // in case realloc() moved the block, or it's newly calloc'd.
        fh->st.st_mtime = time(NULL); // The last "m"odification of the contents of the file/directory
        fh->dirty = 1;
        memcpy( &contents[offset], buffer, size );
    }

    return (ssize_t)size;
}


int truncateFH( tFileHandle * fh, off_t offset )
{
    fh->st.st_mtime = time(NULL); // The last "m"odification of the contents of the file

    if ( offset == 0 )
    {
        if ( fh->contents != NULL )
        {
            free( fh->contents );
            fh->contents = NULL;
        }
        fh->st.st_size = 0;
    }
    else if ( offset > 0 )
    {
        if ( fh->contents == NULL )
        {
            fh->contents = calloc( offset, sizeof( byte ));
        }
        else {
            fh->contents = realloc( fh->contents, offset );
        }
        fh->st.st_size = offset;
    }
    else {
        logError( "attempted to truncate using a negative offset: %ld", offset );
    }
    return 0;
}


int populateFH( tFileHandle * fh )
{
    int result = -EINVAL;

    if ( fh != NULL )
    {
        /* ToDo: populate the contents from LibElektra */
        /* ToDo: set the ctime & mtime from the 'last changed' timestamps of the enclosed UCI values */
        if ( fh->contents != NULL)
        {
            free( fh->contents );
            fh->contents = NULL;
            fh->st.st_size = 0;
        }

        /* ToDo: until we wire up LibElekta, set the contents to something */
        int len = asprintf( (char **)&fh->contents, "Placeholder for %s\n", fh->path );
        if ( len < 0 )
        {
            result = -errno;
            logError( "unable to populate %s", fh->path );
        }
        else {
            fh->st.st_size = len;
            result = 0;
        }
    }

    return result;
}

char * storeSection( char * key, const struct uci_section * section )
{
    /* ToDo: establish the section in libelektra (i.e. check it exists, create it if not) */
    logDebug( "establish section \'%s\'", key );

    /* ToDo: set section->type as metadata on the section  */
    logDebug( "set section \'%s\' to type \'%s\'", key, section->type );

    struct uci_element * optionElement;
    uci_foreach_element( &section->options, optionElement )
    {
        struct uci_option * option = uci_to_option( optionElement );
        switch ( option->type )
        {
        case UCI_TYPE_STRING:
            key = appendKey( key, option->e.name );

            logDebug( "%s = \'%s\'", key, option->v.string );
            /* ToDo: write to libelektra */
            break;

        case UCI_TYPE_LIST:
            {
                key = appendKey( key, option->e.name );
                logDebug( "establish list \'%s\'", key );

                int index = 0;
                char indexStr[32];

                struct uci_element * listElement;
                uci_foreach_element( &option->v.list, listElement )
                {
                    snprintf( indexStr, sizeof(indexStr), "#%03d", index++ );
                    key = appendKey( key, indexStr );
                    logDebug( "%s = '%s'", key, listElement->name );
                    /* ToDo: write to libelektra */
                    key = trimKey( key );
                }
            }
            break;

        default:
            key = appendKey( key, "<error>" );
            logError( "option '%s' has an unknown type (%d)",
                      option->e.name,
                      option->type );
            break;
        }
        key = trimKey( key );
    }
    return key;
}

/* ToDo: mirror the imported UCI structures into libelektra */
void uci2elektra( const struct uci_context * ctx )
{
    char * key = strdup( "system:/config" );
    struct uci_element * element;
    tSection * anonSections = NULL;

    /* ToDo: establish the config root in libelektra (i.e. check it exists, create it if not) */
    logDebug( "establish config root node \'%s\'", key );

    uci_foreach_element( &ctx->root, element )
    {
        struct uci_package * packageElement = uci_to_package( element );
        struct uci_element * sectionElement;

        key = appendKey( key, packageElement->e.name );
        /* ToDo: establish the package (i.e. check it exists, create it if not) */
        logDebug( "establish package \'%s\'", key );

        /* Scan the list of sections, looking for anonymous ones */
        uci_foreach_element( &packageElement->sections, sectionElement )
        {
            struct uci_section * section = uci_to_section( sectionElement );
            if ( section->anonymous ) {
                /* if it's anonymous, then it does not have a unique name, only a type. So we pre-scan the
                 * list of sections to build a list of the types of anonymous sections, */
                tSection * s;
                tHash typeHash = hashString( section->type );
                logDebug( "anonymous section type: \'%s\' (0x%lx)", section->type, typeHash );
                for ( s = anonSections; s != NULL; s = s->next  )
                {
                    if ( s->hash == typeHash )
                    {
                        /* found it, so bump up the count */
                        s->count++;
                        break;
                    }
                }
                if ( s == NULL )
                {
                    /* first time we've seen this section type, so add it to the list */
                    s = calloc( 1, sizeof( tSection ) );
                    if ( s != NULL )
                    {
                        s->hash  = typeHash;
                        s->count = 1;
                        s->next  = anonSections;
                        anonSections = s;
                    }
                }
            }
        }
#ifdef DEBUG
        for ( tSection * s = anonSections; s != NULL; s = s->next  )
        {
            logDebug( "anon section: 0x%lx %d", s->hash, s->count );
        }
#endif

        uci_foreach_element( &packageElement->sections, sectionElement )
        {
            struct uci_section * section = uci_to_section( sectionElement );

            if ( section->anonymous ) {
                /* now use the list of anonymous types we built earlier to generate keys of the
                 * form /{type}/{index} so they are unique when multiple anonymous sections with
                 * the same type can co-exist exist within the same package */
                logDebug( "section type: \'%s\'", section->type );
                key = appendKey( key, section->type);
                tHash typeHash = hashString( section->type );
                for ( tSection * s = anonSections; s != NULL; s = s->next  )
                {
                    if ( s->hash == typeHash )
                    {
                        char indexStr[32];
                        snprintf( indexStr, sizeof(indexStr), "#%03d", s->counter++ );
                        key = appendKey( key, indexStr );

                        key = storeSection( key, section );

                        key = trimKey( key );
                        break;
                    }
                }
            } else {
                logDebug( "section type: \'%s\' name: \'%s\'", section->type, section->e.name );
                key = appendKey( key, section->e.name);

                key = storeSection( key, section );
            }
            key = trimKey( key );
        }
        key = trimKey( key );
    }

    /* clean up the anonSection list */
    for ( tSection * s = anonSections; s != NULL; )
    {
        tSection * next = s->next;
        free( s );
        s = next;
    }
}


int parseFH( tFileHandle * fh )
{
    int result = -EINVAL;

    if ( fh != NULL)
    {
        if ( fh->contents != NULL  )
        {
            if ( fh->st.st_size > 0 )
            {
                logDebug( "contents of %s:", fh->path );
                logTextBlock( kLogDebug, fh->contents, fh->st.st_size );

                /* use libuci to parse the contents into UCI structures */
                struct uci_context * ctx = uci_alloc_context();

                if ( ctx != NULL )
                {
                    const char *name = fh->path;
                    if (*name == '/') ++name;

                    struct uci_package * package = NULL;
                    FILE * contentStream = fmemopen( fh->contents, fh->st.st_size, "r");

                    result = uci_import( ctx, contentStream, name, &package, false);
                    if ( result != 0 )
                    {
                        char * errStr;
                        uci_get_errorstr( ctx, &errStr, "" );
                        logError( " problem importing %s: %s", name, errStr );
                    }
                    else
                    {
                        uci2elektra( ctx );
                    }
                    uci_free_context(ctx);
                }
            }

            fh->dirty = 0;

#if 0
            /* ToDDo: drop the contents, we're done parsing it */
            free( fh->contents );
            fh->contents = NULL;
            fh->st.st_size = 0;
#endif
        }

        result = 0;
    }

    return result;
}



void releaseFH( tFileHandle * fh )
{
    if ( fh != NULL )
    {
        if ( fh->path != NULL )
        {
            free( (void *)fh->path );
        }
        free( fh );
    }
}


int releaseRoot( tMountPoint * mountPoint )
{
    int result = 0;

    if ( mountPoint != NULL )
    {
        tFileHandle * fh = mountPoint->rootFiles;
        mountPoint->rootFiles = NULL;
        mountPoint->rootStat.st_nlink = 0;
        while ( fh != NULL )
        {
            tFileHandle * next = fh->next;
            releaseFH( fh );
            fh = next;
        }
        free( mountPoint );
    }

    return result;
}

int populateRoot( tMountPoint * mountPoint )
{
    int result = 0;

    if ( mountPoint == NULL )
    {
        logError("mountPoint structure is absent");
        return -EFAULT;
    }

    time_t now = time(NULL);

    /* if we recently populated, then assume it's unlikely
     * anything has changed, and reuse what we just built */
    time_t age = now - mountPoint->lastUpdated;
    if ( age < 5 )
    {
        return 0;
    }

    logDebug( "root cache is %lu secs old, so rebuild", age );

    mountPoint->lastUpdated = now;

    mountPoint->rootStat.st_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH; // 0644
    mountPoint->rootStat.st_size = 1024;

    mountPoint->rootStat.st_atime = time(NULL); // The last "a"ccess of the file/directory is right now
    mountPoint->rootStat.st_mtime = time(NULL); // The last "m"odification of the file/directory is right now
    mountPoint->rootStat.st_ctime = time(NULL); // The last "c"hange of the file/directory is right now

    int buildCount = ++mountPoint->buildCounter;

    /* iterate through the current list of UCI files, marking the ones that still
     * exist with the new buildCount, and adding new ones */
    tFileHandle * fh;
    const char * path;
    int i;
    for ( i = 0; (path = iterateUCIfiles( i )) != NULL; ++i )
    {
        fh = findFH( path );
        if ( fh == NULL )
        {
            // did not find a matching entry in the list, so create a new one and add it
            fh = newFH( path, 0 );
            if (fh != NULL)
            {
                /* fill in the contents */
                populateFH( fh );
            }
        }
        if ( fh != NULL )
        {
            /* mark fh as 'seen' by updating the buildCount */
            fh->buildCount = buildCount;
        }
    }

    mountPoint->rootStat.st_nlink = i + 2; /* +2 to include '.' and '..' entries */

    /* now scan the list and remove anything that wasn't just marked with the new buildCount */
    tFileHandle ** prev = &mountPoint->rootFiles;
    for ( fh = mountPoint->rootFiles; fh != NULL; fh = *prev )
    {
        prev = &fh->next;
        if ( fh->buildCount != buildCount )
        {
            /* a stale buildCount value means it's a 'dead' entry - i.e. a LibElektra entry that
             * is no longer being returned by iterateUCIfiles(). So unlink and dispose of it */
            logDebug( "remove \'%s\'", fh->path );
            *prev = fh->next;
            releaseFH( fh );
        }
    }

    return result;
}

int isDirectory( const char * path )
{
    /* the only directory is at the root of the mount */
    return ( path[0] == '/' && path[1] == '\0' );
}

int getFileAttributes( tFileHandle * fh, struct stat * st )
{
    int result = -EINVAL;

    if ( fh != NULL )
    {
        result = populateFH( fh );
        fh->st.st_atime = time(NULL); // The last "a"ccess of the file/directory is right now
        memcpy( st, (const void *)&fh->st, sizeof( struct stat ));
    }

    return result;
}

int getDirAttributes( tMountPoint * mountPoint, struct stat * st )
{
    int result = -EINVAL;

    if ( mountPoint != NULL )
    {
        result = populateRoot( mountPoint );
        mountPoint->rootStat.st_atime = time(NULL); // The last "a"ccess of the file/directory is right now
        memcpy( st, (const void *)&mountPoint->rootStat, sizeof( struct stat ));
    }

    return result;
}

/* Note: this is called really early, mountPoint can't be retrieved until _after_ this has returned */

tMountPoint * initRoot( uid_t uid, gid_t gid )
{
    tMountPoint * mountPoint = calloc( 1, sizeof( tMountPoint ));

    if ( mountPoint != NULL)
    {
        mountPoint->rootStat.st_uid = uid;
        mountPoint->rootStat.st_gid = gid;
    }
    else {
        logError( " failed to allocate mountPoint structure" );
    }

    return mountPoint;
}
