/**
    Created by Paul Chambers on 7/19/22.
*/

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

#define FUSE_USE_VERSION 30
#include <fuse3/fuse.h>

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored. The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given. In that case it is passed to userspace,
 * but libfuse and the kernel will still assign a different
 * inode for internal use (called the "nodeid").
 *
 * `fi` will always be NULL if the file is not currently open, but
 * may also be NULL if the file is open.
 */
static int do_getattr( const char *path,
                       struct stat *st,
                       struct fuse_file_info * info )
{
    printf( "[getattr] Called\n" );
    printf( "\tAttributes of %s requested\n", path );

    // GNU's definitions of the attributes (http://www.gnu.org/software/libc/manual/html_node/Attribute-Meanings.html):
    //   st_uid: 	The user ID of the file’s owner.
    //	 st_gid: 	The group ID of the file.
    //	 st_atime: 	This is the last access time for the file.
    //	 st_mtime: 	This is the time of the last modification to the contents of the file.
    //	 st_mode: 	Specifies the mode of the file. This includes file type information (see Testing File Type)
    //	            and the file permission bits (see Permission Bits).
    //	 st_nlink: 	The number of hard links to the file. This count keeps track of how many directories have
    //	            entries for this file. If the count is ever decremented to zero, then the file itself is
    //	            discarded as soon as no process still holds it open. Symbolic links are not counted in the total.
    //	 st_size:	This specifies the size of a regular file in bytes. For files that are really devices this
    //	            field isn’t usually meaningful. For symbolic links this specifies the length of the file
    //	            name the link refers to.

    st->st_uid = getuid();  // The owner of the file/directory is the user who mounted the filesystem
    st->st_gid = getgid();  // The group of the file/directory is the same as the group of the user
                            // who mounted the filesystem
    st->st_atime = time( NULL ); // The last "a"ccess of the file/directory is right now
    st->st_mtime = time( NULL ); // The last "m"odification of the file/directory is right now

    if ( strcmp( path, "/" ) == 0 )
    {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;  // Why "two" hardlinks instead of "one"? You can find the
                           // answer here: http://unix.stackexchange.com/a/101536
    }
    else
    {
        st->st_mode = S_IFREG | 0644;
        st->st_nlink = 1;
        st->st_size = 1024;
    }

    return 0;
}

/** Read directory
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 */
static int do_readdir( const char *path,
                       void *buffer,
                       fuse_fill_dir_t filler,
                       off_t offset,
                       struct fuse_file_info *fi,
                       enum fuse_readdir_flags )
{
    printf( "--> Getting The List of Files of %s\n", path );

    filler( buffer, ".",  NULL, 0, 0 ); // this Directory (self)
    filler( buffer, "..", NULL, 0, 0 ); // Parent Directory

    if ( strcmp( path, "/" ) == 0 ) // If the user is trying to show the contents of the root directory, show the following
    {
        filler( buffer, "network",  NULL, 0, 0 );
        filler( buffer, "system",   NULL, 0, 0 );
        filler( buffer, "wireless", NULL, 0, 0 );
    }

    return 0;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.	 An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 */
static int do_read( const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi )
{
    printf( "--> Trying to read %s, %lu, %lu\n", path, offset, size );

    char networkText[]  = "placeholder for network UCI file\n";
    char systemText[]   = "placeholder for system UCI file\n";
    char wirelessText[] = "placeholder for wireless UCI file\n";
    char *selectedText  = NULL;

    // ... //

    if ( strcmp( path, "/network" ) == 0 )
        selectedText = networkText;
    else if ( strcmp( path, "/system" ) == 0 )
        selectedText = systemText;
    else if ( strcmp( path, "/wireless" ) == 0 )
        selectedText = wirelessText;
    else
        return -1;

    // ... //

    memcpy( buffer, selectedText + offset, size );

    return strlen( selectedText ) - offset;
}

static struct fuse_operations operations = {
    .getattr  = do_getattr,
    .readdir  = do_readdir,
    .read     = do_read,
};

int main( int argc, char *argv[] )
{
    return fuse_main( argc, argv, &operations, NULL );
}