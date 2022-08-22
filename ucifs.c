/**
    Created by Paul Chambers on 7/19/22.

    requires libfuse3-dev package to be installed
*/

#define _GNU_SOURCE

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>

#define FUSE_USE_VERSION 35
#include <fuse3/fuse.h>

/* do this explicitly, as builds on x86-64 define it as zero */
#undef  O_LARGEFILE
#define O_LARGEFILE 0x00008000

#include "uci2libelektra.h"
#include "logStuff.h"

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

tMountPoint * getMountPoint( void )
{
    struct fuse_context * fc = fuse_get_context();
    if ( fc == NULL || fc->private_data == NULL)
    {
        logError( "unable to retrieve mountPoint structure" );
        return NULL;
    }
    return (tMountPoint *) fc->private_data;
}

void setUserGroup( struct stat * st )
{
    struct fuse_context * fc = fuse_get_context();
    if ( fc != NULL)
    {
        st->st_uid = fc->uid;
        st->st_gid = fc->gid;
    }
}

tFileHandle * fetchFH(  struct fuse_file_info * fi, const char * path )
{
    tFileHandle * result = NULL;

    if ( fi != NULL )
    {
        result = (tFileHandle *) fi->fh;
    }
    if (result == NULL)
    {
        result = getFH( NULL, path );
    }
    if ( result != NULL && fi != NULL )
    {
        fi->fh = (uint64_t)result;
    }

    return result;
}

/**
 * Initialize filesystem
 *
 * The return value will passed in the `private_data` field of `struct fuse_context`
 * to all file operations, and as a parameter to the destroy() method. It overrides
 * the initial value provided to fuse_main() / fuse_new().
 */
static void * doInit( struct fuse_conn_info * conn,
                      struct fuse_config *    cfg )
{
    (void)conn;

    logDebug( "### op: init" );

    void * result = (void *)initRoot( cfg->uid, cfg->gid );
    logDebug( "mountPoint %p", result );

    return result;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 */
static void doDestroy( void * private_data )
{
    errno = 0; logDebug( "### op: destroy" );

    if ( private_data != NULL )
    {
        releaseRoot( (tMountPoint *)private_data );
    }
}


/**
 * Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are ignored. The 'st_ino' field is
 * ignored except if the 'use_ino' mount option is given. In that case it is passed to userspace,
 * but libfuse and the kernel will still assign a different inode for internal use (called the "nodeid").
 *
 * `fuse_file_info` will always be NULL if the file is not currently open, but may also be NULL if
 * the file is open. Thus we cannot count on being able to retrieve the opaque value in fi->fh.
**/
static int doGetAttr( const char * path,
                      struct stat * st,
                      struct fuse_file_info * fi )
{
    int result = -ENOENT;

    logDebug( "### op: getattr \'%s\' [%p]", path, fi );

    if ( isDirectory( path ) )
    {
        tMountPoint * mountPoint = getMountPoint();
        if ( mountPoint != NULL )
        {
            result = getDirAttributes( mountPoint, st );
        }
    }
    else
    {
        tFileHandle * fh = fetchFH( fi, path );
        if ( fh != NULL )
        {
            result = getFileAttributes( fh, st );
        }
    }

    return result;
}

/* * * * * * * * * * * * * * * * * * * * * *
 * * * * * * Directory Operations  * * * * *
 * * * * * * * * * * * * * * * * * * * * * */

/** Open directory
 *
 * Unless the 'default_permissions' mount option is given, this method should check
 * opendir is permitted for this directory. Optionally opendir may also return an
 * arbitrary filehandle in the fuse_file_info structure, which will be passed to
 * readdir, releasedir and fsyncdir.
 */
int doOpenDir( const char * path, struct fuse_file_info * fi )
{
    (void)fi;

    logDebug("opendir \'%s\' [%p]", path, fi );

    int result = -EINVAL;

    if ( path[0] == '/' && path[1] == '\0' )
    {
        tMountPoint * mountPoint = getMountPoint();
        if ( mountPoint != NULL )
        {
            /* ToDo: check permissions */
            result = populateRoot( mountPoint );
        }
    }

    return result;
}

/**
 * Read directory
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and passes zero to the
 *    filler function's offset. The filler function will not return '1' (unless an
 *    error happens), so the whole directory is read in a single readdir operation.
 *
 * 2) The readdir implementation keeps track of the offsets of the directory entries.
 *    It uses the offset parameter and always passes non-zero offset to the filler
 *    function.  When the buffer is full (or an error happens) the filler function
 *    will return '1'.
**/
static int doReadDir( const char * path,
                      void * buffer,
                      fuse_fill_dir_t filler,
                      off_t offset,
                      struct fuse_file_info * fi,
                      enum fuse_readdir_flags flags )
{
    (void)offset; (void)fi; (void)flags;

    logDebug( "### op: readdir \'%s\' [%p]", path, fi );

    int result = -EINVAL;

    if ( isDirectory( path ) )
    {
        result = 0;

        filler( buffer, ".",  NULL, 0, 0 );  // this Directory (self)
        filler( buffer, "..", NULL, 0, 0 );  // my parent directory

        tFileHandle * fh = nextFH( NULL );
        while ( fh != NULL )
        {
            const char *  filepath = getFHpath( fh );
            if ( *filepath == '/' ) ++filepath;

            struct stat * filestat = getFHstat( fh );

            filler( buffer, filepath, filestat, 0, 0 );

            fh = nextFH( fh );
        }
    }

    return result;
}

/** Release directory
 */
int doReleaseDir( const char * path, struct fuse_file_info * fi )
{
    logDebug("releasedir \'%s\' [%p]", path, fi );

    return 0;
}

/* * * * * * * * * * * * * * * * * * * * * *
 * * * * * * * File Operations * * * * * * *
 * * * * * * * * * * * * * * * * * * * * * */

/**
 * Open a file
 *
 * Open flags are available in fi->flags. The following rules apply.
 *
 *  - Creation (O_CREAT, O_EXCL, O_NOCTTY) flags will be filtered out / handled by the kernel.
 *
 *  - Access modes (O_RDONLY, O_WRONLY, O_RDWR, O_EXEC, O_SEARCH) should be used by the filesystem
 *    to check if the operation is permitted.  If the ``-o default_permissions`` mount option is
 *    given, this check is already done by the kernel before calling open() and may thus be omitted
 *    by the filesystem.
 *
 *  - When writeback caching is enabled, the kernel may send read requests even for files opened
 *    with O_WRONLY. The filesystem should be prepared to handle this.
 *
 *  - When writeback caching is disabled, the filesystem is expected to properly handle the
 *    O_APPEND flag and ensure that each write is appending to the end of the file.
 *
 *  - When writeback caching is enabled, the kernel will handle O_APPEND. However, unless all
 *    changes to the file come through the kernel this will not work reliably. The filesystem
 *    should thus either ignore the O_APPEND flag (and let the kernel handle it), or return
 *    an error (indicating that reliably O_APPEND is not available).
 *
 * Filesystem may store an arbitrary file handle (pointer, index, etc) in fi->fh, and use
 * this in other all other file operations (read, write, flush, release, fsync).
 *
 * Filesystem may also implement stateless file I/O and not store anything in fi->fh.
 *
 * There are also some flags (direct_io, keep_cache) which the filesystem may set in fi, to
 * change the way the file is opened. See fuse_file_info structure in <fuse_common.h> for
 * more details.
 *
 * If this request is answered with an error code of ENOSYS and FUSE_CAP_NO_OPEN_SUPPORT is
 * set in `fuse_conn_info.capable`, this is treated as success and future calls to open will
 * also succeed without being send to the filesystem process.
**/

static int doOpen( const char * path, struct fuse_file_info * fi )
{
    logDebug( "### op: open \'%s\' %s [%p]", path, openFlagsAsStr(fi->flags), fi );

    int result = 0;

    if ( fi != NULL )
    {
        tFileHandle * fh = fetchFH( fi, path );
        if ( fh == NULL )
            result = -ENOENT;
        else {
            if ( fi->flags & O_TRUNC )
            {
                logDebug( "  \'%s\' truncated", path );
                truncateFH( fh, 0 );
            }
            result = populateFH( fh );
        }
    }

    return result;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel versions earlier than 2.6.15,
 * the mknod() and open() methods will be called instead.
**/
static int doCreate(const char * path, mode_t mode, struct fuse_file_info * fi)
{
    int result = 0;
    logDebug( "### op: create \'%s\' (0x%x) %s [%p]", path, mode, createModeAsStr(mode), fi );

    tFileHandle * fh = findFH( path );
    /* we are not expecting to find a match, i.e. fh will be NULL */
    if ( fh == NULL )
    {
        fh = newFH( path, mode );
        if ( fh != NULL &&  fi != NULL )
        {
            fi->fh = (uint64_t)fh;
        }
    }
    if ( fh != NULL )
    {
        result = doOpen( path, fi );
    }

    return result;
}

/** Change the size of a file
 *
 * `fi` will always be NULL if the file is not currently open, but may also be NULL if the file is open.
 *
 * Unless FUSE_CAP_HANDLE_KILLPRIV is disabled, this method is expected to reset the setuid and setgid bits.
 */
int doTruncate(const char * path, off_t offset, struct fuse_file_info * fi)
{
    int result = 0;

    logDebug( "### op: truncate \'%s\' @%lu [%p]", path, offset, fi );

    tFileHandle * fh = fetchFH( fi, path );
    if (fh == NULL)
        result = -ENOENT;
    else {
        result = truncateFH( fh, offset );
    }
    return result;
}

/**
 * Release an open file
 *
 * Release is called when there are no more references to an open file: all file descriptors
 * are closed and all memory mappings are unmapped.
 *
 * For every open() call there will be exactly one release() call with the same flags and
 * file handle.  It is possible to have a file opened more than once, in which case only
 * the last release will mean, that no more reads/writes will happen on the file.
 *
 * The return value of release is ignored.
 */

static int doRelease( const char * path, struct fuse_file_info * fi )
{
    int result = 0;

    logDebug( "### op: release \'%s\' [%p]", path, fi );

    tFileHandle * fh = fetchFH( fi, path );
    if ( fh == NULL )
        result = -ENOENT;
    else {
        result = parseFH( fh );
    }

    return result;
}

/**
 * Read data from an open file
 *
 * Read should return exactly the number of bytes requested except on EOF or error,
 * otherwise the rest of the data will be substituted with zeroes. An exception to
 * this is when the 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of this operation.
**/

static int doRead( const char *path,
                   char *buffer,
                   size_t size,
                   off_t offset,
                   struct fuse_file_info * fi )
{
    (void)fi;

    logDebug( "### op: read \'%s\' @%lu (%lu) [%p]\n", path, offset, size, fi );

    ssize_t length = size;

    tFileHandle * fh = fetchFH( fi, path );
    if ( fh == NULL )
        length = -ENOENT;
    else
        length = readFH( fh, buffer, size, offset );

    return (int)length;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested except on error.
 * An exception to this is when the 'direct_io' mount option is specified
 * (see read operation).
 *
 * Unless FUSE_CAP_HANDLE_KILLPRIV is disabled, this method is expected to
 * reset the setuid and setgid bits.
**/

static int doWrite( const char * path,
                    const char * buffer,
                    size_t size,
                    off_t offset,
                    struct fuse_file_info * fi )
{
    (void)buffer;

    logDebug( "### op: write %s @%lu (%lu) [%p]", path, offset, size, fi );

    tFileHandle * fh = fetchFH( fi, path );
    if ( fh == NULL )
        size = -ENOENT;
    else {
        // ToDo: check permissions
        size = writeFH( fh, buffer, size, offset );
    }

    return size;
}

#ifdef DEBUG

/**
 *  stub implementations to log that an unimplemented entry point has been called.
 *  this is only done in debug builds. The libfuse source code returns -ENOSYS if
 *  the operation handler is NULL, so behave the same way
**/

/* temporarily turn off the 'unused parameter' warning, since these are only stubs and that is expected */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

/** Read the target of a symbolic link
 *
 * The buffer should be filled with a null terminated string. The buffer size argument
 * includes the space for the terminating null character. If the linkname is too long
 * to fit in the buffer, it should be truncated. The return value should be 0 for success.
 */
int doReadLink( const char * path, char *, size_t )
{
    logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/** Create a file node
 *
 * This is called for creation of all non-directory, non-symlink nodes.  If the filesystem
 * defines a create() method, then for regular files that will be called instead.
 */
int doMkNod( const char * path, mode_t mode, dev_t dev )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path);
	return -ENOSYS;
}

/** Create a directory
 *
 * Note that the mode argument may not have the type specification bits set, i.e.
 * S_ISDIR(mode) can be false.  To obtain the correct directory type bits use mode|S_IFDIR
 */
int doMkDir( const char * path, mode_t mode )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/** Remove a file */
int doUnlink( const char * path )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/** Remove a directory */
int doRmDir( const char * path )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/** Create a symbolic link */
int doSymlink( const char * path, const char * )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/** Rename a file
 *
 * *flags* may be `RENAME_EXCHANGE` or `RENAME_NOREPLACE`. If RENAME_NOREPLACE is specified,
 * the filesystem must not overwrite *newname* if it exists and return an error instead. If
 * `RENAME_EXCHANGE` is specified, the filesystem must atomically exchange the two files,
 * i.e. both must exist and neither may be deleted.
 */
int doRename( const char * path, const char *, unsigned int flags )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/** Create a hard link to a file */
int doLink( const char * path, const char * )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/** Change the permission bits of a file
 *
 * `fi` will always be NULL if the file is not currently open, but may also be NULL if the file is open.
 */
int doChMod( const char * path, mode_t mode, struct fuse_file_info * fi )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/** Change the owner and group of a file
 *
 * `fi` will always be NULL if the file is not currently open, but may also be NULL if the file is open.
 *
 * Unless FUSE_CAP_HANDLE_KILLPRIV is disabled, this method is expected to reset the setuid and setgid bits.
 */
int doChOwn( const char * path, uid_t uid, gid_t gid, struct fuse_file_info *fi )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/** Get file system statistics
 *
 * The 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
 */
int doStatFS( const char * path, struct statvfs * )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/** Possibly flush cached data
 *
 * BIG NOTE: This is not equivalent to fsync().  It's not a request to sync dirty data.
 *
 * Flush is called on each close() of a file descriptor, as opposed to release which is
 * called on the close of the last file descriptor for a file.  Under Linux, errors
 * returned by flush() will be passed to userspace as errors from close(), so flush()
 * is a good place to write back any cached dirty data. However, many applications
 * ignore errors on close(), and on non-Linux systems, close() may succeed even if
 * flush() returns an error. For these reasons, filesystems should not assume that
 * errors returned by flush will ever be noticed or even delivered.
 *
 * NOTE: The flush() method may be called more than once for each open().  This happens
 * more than one file descriptor refers to an open file handle, e.g. due to dup(),
 * dup2() or fork() calls.  It is not possible to determine if a flush is final, so
 * each flush should be treated equally.  Multiple write-flush sequences are relatively
 * rare, so this shouldn't be a problem.
 *
 * Filesystems shouldn't assume that flush will be called at any particular point.
 * It may be called more times than expected, or not at all.
 *
 * [close]: http://pubs.opengroup.org/onlinepubs/9699919799/functions/close.html
 */
int doFlush( const char * path, struct fuse_file_info * fi )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/** Synchronize file contents
 *
 * If the datasync parameter is non-zero, then only the user data should be flushed, not the meta data.
 */
int doFSync( const char * path, int, struct fuse_file_info * fi)
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/** Set extended attributes */
int doSetXAttr( const char * path, const char *, const char *, size_t, int )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/** Get extended attributes */
int doGetXAttr( const char * path, const char *, char *, size_t )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/** List extended attributes */
int doListXAttr( const char * path, char *, size_t )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/** Remove extended attributes */
int doRemoveXAttr( const char * path, const char * )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/** Synchronize directory contents
 *
 * If the datasync parameter is non-zero, then only the user data should be flushed, not the meta data
 */
int doFSyncDir( const char * path, int, struct fuse_file_info * fi )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/**
 * Check file access permissions
 *
 * This will be called for the access() system call.  If the 'default_permissions' mount option is given,
 * this method is not called.
 *
 * This method is not called under Linux kernel versions 2.4.x
**/
int doAccess( const char * path, int )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/**
 * Perform POSIX file locking operation
 *
 * The cmd argument will be either F_GETLK, F_SETLK or F_SETLKW.
 *
 * For the meaning of fields in 'struct flock' see the man page for fcntl(2).
 * The l_whence field will always be set to SEEK_SET.
 *
 * For checking lock ownership, the 'fuse_file_info->owner' argument must be used.
 *
 * For F_GETLK operation, the library will first check currently held locks, and
 * if a conflicting lock is found it will return information without calling this
 * method. This ensures, that for local locks the l_pid field is correctly filled
 * in. The results may not be accurate in case of race conditions and in the
 * presence of hard links, but it's unlikely that an application would rely on
 * accurate GETLK results in these cases.  If a conflicting lock is not found,
 * this method will be called, and the filesystem may fill out l_pid by a
 * meaningful value, or it may leave this field zero.
 *
 * For F_SETLK and F_SETLKW the l_pid field will be set to the pid of the process
 * performing the locking operation.
 *
 * Note: if this method is not implemented, the kernel will still allow file
 * locking to work locally.  Hence it is only interesting for network filesystems
 * and similar.
**/
int doLock( const char * path,
            struct fuse_file_info * fi,
            int cmd,
            struct flock * )
{
    logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/**
 * Change the access and modification times of a file with nanosecond resolution
 *
 * This supersedes the old utime() interface. New applications should use this.
 *
 * `fi` will always be NULL if the file is not currently open, but may also be
 * NULL if the file is open.
 *
 * See the utimensat(2) man page for details.
 */
int doUtimeNS( const char * path, const struct timespec tv[2], struct fuse_file_info * fi )
{
    logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/**
 * Map block index within file to block index within device
 *
 * Note: This makes sense only for block device backed filesystems mounted
 * with the 'blkdev' option
 */
int doBMap( const char * path, size_t blocksize, uint64_t * idx )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/**
 * Ioctl
 *
 * flags will have FUSE_IOCTL_COMPAT set for 32bit ioctls in 64bit environment.
 * The size and direction of data is determined by _IOC_*() decoding of cmd.
 * For _IOC_NONE, data will be NULL, for _IOC_WRITE data is out area, for
 * _IOC_READ in area and if both are set in/out area.  In all non-NULL cases,
 * the area is of _IOC_SIZE(cmd) bytes.
 *
 * If flags has FUSE_IOCTL_DIR then the fuse_file_info refers to a directory file handle.
 *
 * Note : the unsigned long request submitted by the application is truncated to 32 bits.
 */
int doIoctl( const char * path,
             unsigned int cmd,
             void * arg,
             struct fuse_file_info * fi,
             unsigned int flags,
             void *data )
{
    logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/**
 * Poll for IO readiness events
 *
 * Note: If ph is non-NULL, the client should notify when IO readiness events
 * occur by calling fuse_notify_poll() with the specified ph.
 *
 * Regardless of the number of times poll with a non-NULL ph is received, single
 * notification is enough to clear all. Notifying more times incurs overhead
 * but doesn't harm correctness.
 *
 * The callee is responsible for destroying ph with fuse_pollhandle_destroy()
 * when no longer in use.
 */
int doPoll( const char * path,
            struct fuse_file_info *,
            struct fuse_pollhandle * ph,
            unsigned * reventsp )
{
    logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/** Write contents of buffer to an open file
 *
 * Similar to the write() method, but data is supplied in a generic buffer. Use
 * fuse_buf_copy() to transfer data to the destination.
 *
 * Unless FUSE_CAP_HANDLE_KILLPRIV is disabled, this method is expected to reset
 * the setuid and setgid bits.
 *
 * NOTE: not implemented, since fuse will fall back to doWrite()
 */

/** Store data from an open file in a buffer
 *
 * Similar to the read() method, but data is stored and returned in a generic buffer.
 *
 * No actual copying of data has to take place, the source file descriptor may simply
 * be stored in the buffer for later data transfer.
 *
 * The buffer must be allocated dynamically and stored at the location pointed to by
 * bufp.  If the buffer contains memory regions, they too must be allocated using
 * malloc().  The allocated memory will be freed by the caller.
 *
 * NOTE: not implemented, since fuse will fall back to doRead()
 */

/**
 * Perform BSD file locking operation
 *
 * The op argument will be either LOCK_SH, LOCK_EX or LOCK_UN
 *
 * Nonblocking requests will be indicated by ORing LOCK_NB to the above operations
 *
 * For more information see the flock(2) manual page.
 *
 * Additionally fi->owner will be set to a value unique to this open file. This same
 * value will be supplied to ->release() when the file is released.
 *
 * Note: if this method is not implemented, the kernel will still allow file locking
 * to work locally.  Hence it is only interesting for network filesystems and similar.
 */
int doFLock( const char * path,
             struct fuse_file_info * fi,
             int op )
{
    logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/**
 * Allocates space for an open file
 *
 * This function ensures that required space is allocated for specified file. If
 * this function returns success then any subsequent write request to specified
 * range is guaranteed not to fail because of lack of space on the file system media.
 */
int doFAllocate( const char * path,
                 int ,
                 off_t offset,
                 off_t ,
                 struct fuse_file_info * fi )
{
    logDebug( "--- nop: %s \'%s\'", __func__, path );
	return -ENOSYS;
}

/**
 * Copy a range of data from one file to another
 *
 * Performs an optimized copy between two file descriptors without the additional
 * cost of transferring data through the FUSE kernel module to user space (glibc)
 * and then back into the FUSE filesystem again.
 *
 * In case this method is not implemented, applications are expected to fall back
 * to a regular file copy.   (Some glibc versions did this emulation automatically,
 * but the emulation has been removed from all glibc release branches.)
**/
ssize_t doCopyFileRange( const char * path_in,
                         struct fuse_file_info * fi_in,
                         off_t offset_in,
                         const char * path_out,
                         struct fuse_file_info * fi_out,
                         off_t offset_out,
                         size_t size,
                         int flags )
{
    logDebug( "--- nop: %s \'%s\'", __func__, path_in );
	return (ssize_t)-ENOSYS;
}

/**
 * Find next data or hole after the specified offset
 */
off_t doLSeek( const char * path, off_t offset, int whence, struct fuse_file_info * fi )
{
	logDebug( "--- nop: %s \'%s\'", __func__, path );
	return (off_t)-ENOSYS;
}

#pragma GCC diagnostic pop
#endif

/* * * * * * * * * * * * * * * * * * * * * *
 * * * * * * * * * Startup * * * * * * * * *
 * * * * * * * * * * * * * * * * * * * * * */

static struct fuse_operations operations = {
     /* hierarchy navigation */
    .getattr         = doGetAttr,

     /* fuse operations */
    .init            = doInit,
    .destroy         = doDestroy,

     /* directory operations */
    .opendir         = doOpenDir,
    .readdir         = doReadDir,
    .releasedir      = doReleaseDir,

     /* file operations */
    .create          = doCreate,
    .open            = doOpen,
    .truncate        = doTruncate,
    .release         = doRelease,
    .read            = doRead,
    .write           = doWrite,

#ifdef DEBUG
    .readlink        = doReadLink,
    .mknod           = doMkNod,
    .mkdir           = doMkDir,
    .unlink          = doUnlink,
    .rmdir           = doRmDir,
    .symlink         = doSymlink,
    .rename          = doRename,
    .link            = doLink,
    .chmod           = doChMod,
    .chown           = doChOwn,
    .statfs          = doStatFS,
    .flush           = doFlush,
    .fsync           = doFSync,
    .setxattr        = doSetXAttr,
    .getxattr        = doGetXAttr,
    .listxattr       = doListXAttr,
    .removexattr     = doRemoveXAttr,
    .fsyncdir        = doFSyncDir,
    .access          = doAccess,
    .lock            = doLock,
    .utimens         = doUtimeNS,
    .bmap            = doBMap,
	.ioctl           = doIoctl,
    .poll            = doPoll,
     // .write_buf   * NOTE: omitted intentionally. In its absence, fuse will fall back to doWrite()
     // .read_buf    * NOTE: omitted intentionally. In its absence, fuse will fall back to doRead()
    .flock           = doFLock,
    .fallocate       = doFAllocate,
    .copy_file_range = doCopyFileRange,
    .lseek           = doLSeek,
#endif
};

int main( int argc, char *argv[] )
{
    int result;

    char * executableName = strrchr( argv[0], '/' );
    if ( executableName++ == NULL )
    {
        executableName = argv[0];
    }

    initLogStuff( executableName );
    setLogStuffDestination( kLogDebug, kLogToSyslog, kLogNormal );

    fprintf(stderr, "starting \'%s\'\n", executableName );
    logInfo( "starting \'%s\'", executableName );

    result = fuse_main( argc, argv, &operations, NULL );

    return result;
}
