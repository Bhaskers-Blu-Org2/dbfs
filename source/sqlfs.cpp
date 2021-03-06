//****************************************************************************
//      Copyright (c) Microsoft Corporation. All rights reserved.
//      Licensed under the MIT license.
//
// File: sqlfs.cpp
//
// Purpose:
//   This file contains definitions of functions used by the FUSE
//   module for various filesystem operations. 
//
//
#define FUSE_USE_VERSION 26

#include "UtilsPrivate.h"

// ---------------------------------------------------------------------------
// Method: GetattrLocalImpl
//
// Description:
//    This method redirects the getattr system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
GetattrLocalImpl(
    const char* path,
    struct stat* stbuf)
{
    int     result;
    string  fpath;

    fpath = CalculateDumpPath(path);
    result = lstat(fpath.c_str(), stbuf);
    if (result == -1)
    {
        // Not printing the error because this error is quite common
        // and cosmetic.
        //
        result = -errno;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Method: AccessLocalImpl
//
// Description:
//    This method redirects the access system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
AccessLocalImpl(
    const char* path,
    int mask)
{
    int     result;
    string  fpath;

    fpath = CalculateDumpPath(path);
    result = access(fpath.c_str(), mask);
    if (result == -1)
    {
        result = ReturnErrnoAndPrintError(__FUNCTION__, "access failed");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: ReadlinkLocalImpl
//
// Description:
//    This method redirects the readlink system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
ReadlinkLocalImpl(
    const char* path,
    char* buf,
    size_t size)
{
    int     result;
    string  fpath;

    fpath = CalculateDumpPath(path);
    result = readlink(fpath.c_str(), buf, size - 1);
    if (result == -1)
    {
        result = ReturnErrnoAndPrintError(__FUNCTION__, "readlink failed");
    }
    else
    {
        buf[result] = '\0';
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: ReaddirLocalImpl
//
// Description:
//    This method redirects the opendir system call to the dump directory.
//    If this is opening a custom query directory, it will 
//    1. Delete all the previous files in custom query dump directory. 
//       To ensure that if there is a query file is removed or added, 
//       it will be reflected properly.
//    2. Iterate the user custom query directory and create output files
//       with the same name as the query file at custom query dump directory. 
//
// Returns:
//    0 on success and -errno on error.
//
static int
OpendirLocalImpl(
    const char* path,
    struct fuse_file_info* fi)
{
    int             failed = 0;
    DIR*            dp;
    string          fpath;
    vector<string>  tokens;
    string          servername;
    string          userQueriesPath;
    string          filepath;
    
    fpath = CalculateDumpPath(path);
    dp = opendir(fpath.c_str());
    if (dp != NULL)
    {
        // Save file handle for use in readdir and releasedir
        //
        fi->fh = (uint64_t)(dp);

        // If this is a custom query dir, populate dump path with custom query 
        // output files so that readdir can list the files.
        //
        if (strstr(path, CUSTOM_QUERY_FOLDER_NAME))
        {
            RemoveCustomQueriesOutputFiles(dp, fpath);            

            // Need to rewind the directory point to the begining for readdir.
            //
            rewinddir(dp);

            // Tokenising the path.
            //
            tokens = Split(path, '/');
        
            // Path is of the form <servername>/<filename>
            // On success, there will be more than 1 token.
            //
            assert(tokens.size() > 1);
        
            servername = tokens[0];
            assert(strcmp(tokens[1].c_str(), CUSTOM_QUERY_FOLDER_NAME) == 0);

            CreateCustomQueriesOutputFiles(servername, fpath);            
        }
    }
    else
    {
        failed = ReturnErrnoAndPrintError(__FUNCTION__, "opendir failed");
        fi->fh = 0;
    }

    return failed;
}

// ---------------------------------------------------------------------------
// Method: ReaddirLocalImpl
//
// Description:
//    This method redirects the readdir system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
ReaddirLocalImpl(
    const char* path,
    void* buf,
    fuse_fill_dir_t filler,
    off_t offset,
    struct fuse_file_info* fi)
{
    DIR*            dp;
    struct dirent*  de;
    string          fpath;
    struct stat     st;
    int             result = 0;

    (void)offset;
    (void)fi;

    dp = (DIR*)fi->fh;
    if (dp == NULL)
    {
        result = ReturnErrnoAndPrintError(__FUNCTION__, "opendir failed");
    }

    if (!result)
    {
        while ((de = readdir(dp)) != NULL)
        {
            memset(&st, 0, sizeof(st));
            st.st_ino = de->d_ino;
            st.st_mode = de->d_type << 12;
            if (filler(buf, de->d_name, &st, 0))
                break;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: ReleasedirLocalImpl
//
// Description:
//    This method redirects the readdir system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
ReleasedirLocalImpl(
    const char* path,
    struct fuse_file_info* fi)
{
    int             error = 0;
    DIR*            dp;

    dp = (DIR*)fi->fh;
    if (dp != NULL)
    {
        error = closedir(dp);
    }

    return error;
}

// ---------------------------------------------------------------------------
// Method: MknodLocalImpl
//
// Description:
//    This method redirects the mknod system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
MknodLocalImpl(
    const char* path,
    mode_t mode,
    dev_t rdev)
{
    int     result;
    string  fpath;

    fpath = CalculateDumpPath(path);
    if (S_ISREG(mode))
    {
        result = open(fpath.c_str(), O_CREAT | O_EXCL | O_WRONLY, mode);
        if (result >= 0)
        {
            result = close(result);
            if (result)
            {
                result = ReturnErrnoAndPrintError(__FUNCTION__, "close failed");
            }
        }
        else
        {
            result = ReturnErrnoAndPrintError(__FUNCTION__, "open failed");
        }
    }
    else if (S_ISFIFO(mode))
    {
        result = mkfifo(fpath.c_str(), mode);
        if (result)
        {
            result = ReturnErrnoAndPrintError(__FUNCTION__, "mkfifo failed");
        }
    }
    else
    {
        result = mknod(fpath.c_str(), mode, rdev);
        if (result)
        {
            result = ReturnErrnoAndPrintError(__FUNCTION__, "mknod failed");
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: MkdirLocalImpl
//
// Description:
//    This method redirects the mkdir system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
MkdirLocalImpl(
    const char* path,
    mode_t mode)
{
    int     result;
    string  fpath;

    fpath = CalculateDumpPath(path);
    result = mkdir(fpath.c_str(), mode);
    if (result == -1)
    {
        result = ReturnErrnoAndPrintError(__FUNCTION__, "mkdir failed");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: UnlinkLocalImpl
//
// Description:
//    This method redirects the unlink system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
UnlinkLocalImpl(
    const char* path)
{
    int     result;
    string  fpath;

    fpath = CalculateDumpPath(path);
    result = unlink(fpath.c_str());
    if (result == -1)
    {
        result = ReturnErrnoAndPrintError(__FUNCTION__, "unlink failed");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: RmdirLocalImpl
//
// Description:
//    This method redirects the rmdir system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
RmdirLocalImpl(
    const char* path)
{
    int     result;
    string  fpath;

    fpath = CalculateDumpPath(path);
    result = rmdir(fpath.c_str());
    if (result == -1)
    {
        result = ReturnErrnoAndPrintError(__FUNCTION__, "rmdir failed");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: SymlinkLocalImpl
//
// Description:
//    This method redirects the symlink system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
SymlinkLocalImpl(
    const char* from,
    const char* to)
{
    int     result;
    string  fpath;
    string  tpath;

    fpath = CalculateDumpPath(from);
    tpath = CalculateDumpPath(to);
    result = symlink(fpath.c_str(), tpath.c_str());
    if (result == -1)
    {
        result = ReturnErrnoAndPrintError(__FUNCTION__, "symlink failed");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: RenameLocalImpl
//
// Description:
//    This method redirects the rename system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
RenameLocalImpl(
    const char* from,
    const char* to)
{
    int     result;
    string  fpath;
    string  tpath;

    fpath = CalculateDumpPath(from);
    tpath = CalculateDumpPath(to);
    result = rename(fpath.c_str(), tpath.c_str());
    if (result == -1)
    {
        result = ReturnErrnoAndPrintError(__FUNCTION__, "rename failed");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: LinkLocalImpl
//
// Description:
//    This method redirects the link system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
LinkLocalImpl(
    const char* from,
    const char* to)
{
    int     result;
    string  fpath;
    string  tpath;

    fpath = CalculateDumpPath(from);
    tpath = CalculateDumpPath(to);
    result = link(fpath.c_str(), tpath.c_str());
    if (result == -1)
    {
        result = ReturnErrnoAndPrintError(__FUNCTION__, "link failed");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: ChmodLocalImpl
//
// Description:
//    This method redirects the chmod system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
ChmodLocalImpl(
    const char* path,
    mode_t mode)
{
    int     result;
    string  fpath;

    fpath = CalculateDumpPath(path);
    result = chmod(fpath.c_str(), mode);
    if (result == -1)
    {
        result = ReturnErrnoAndPrintError(__FUNCTION__, "chmod failed");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: ChownLocalImpl
//
// Description:
//    This method redirects the chown system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
ChownLocalImpl(
    const char* path,
    uid_t username,
    gid_t gid)
{
    int     result;
    string  fpath;

    fpath = CalculateDumpPath(path);
    result = lchown(fpath.c_str(), username, gid);
    if (result == -1)
    {
        result = ReturnErrnoAndPrintError(__FUNCTION__, "lchown failed");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: TruncateLocalImpl
//
// Description:
//    This method redirects the truncate system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
TruncateLocalImpl(
    const char* path,
    off_t size)
{
    int     result;
    string  fpath;

    fpath = CalculateDumpPath(path);
    result = truncate(fpath.c_str(), size);
    if (result == -1)
    {
        result = ReturnErrnoAndPrintError(__FUNCTION__, "truncate failed");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: UtimensLocalImpl
//
// Description:
//    This method redirects the utimens system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
UtimensLocalImpl(
    const char* path,
    const struct timespec ts[2])
{
    int     result;
    string  fpath;

    fpath = CalculateDumpPath(path);
    // Not using utime/utimes since they follow symlinks.
    //
    result = utimensat(0, fpath.c_str(), ts, AT_SYMLINK_NOFOLLOW);
    if (result == -1)
    {
        result = ReturnErrnoAndPrintError(__FUNCTION__, "utimensat failed");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: GetFileDescriptorForPath
//
// Description:
//    This method checks if an fd is already stored in fuse_file_info
//    else it will open the file at the path provided.
//
// Returns:
//    None. (value of fd would indicate success or failure).
//
static void
GetFileDescriptorForPath(
    const char* path,
    struct fuse_file_info* fi,
    int& fd)
{
    string fpath;

    // If we don't already have a fd, open the file.
    //
    if (!fi)
    {
        fpath = CalculateDumpPath(path);
        // Open the file.
        //
        fd = open(fpath.c_str(), fi->flags);
        if (fd == -1)
        {
            ReturnErrnoAndPrintError(__FUNCTION__, "open failed");
        }
    }
    else
    {
        fd = fi->fh;
    }
}

// ---------------------------------------------------------------------------
// Method: CloseFileDesciptorIfOpened
//
// Description:
//    This method will close the fd provided only if provided fuse_file_info
//    pointer is null - because that is the case when GetFileDescriptorForPath
//    would have opened a fd.
//
// Returns:
//    None.
//
static void
CloseFileDesciptorIfOpened(
    struct fuse_file_info* fi,
    int fd)
{
    if (!fi)
    {
        int result = close(fd);
        if (result)
        {
            result = ReturnErrnoAndPrintError(__FUNCTION__, "close failed");
        }
    }
}

// ---------------------------------------------------------------------------
// Method: GetDmvFileContent
//
// Description:
//    This function is responsible for filling the file(DMV) being opened 
//    with the content of that DMV from the appropriate server and form. 
//
//    The path contains the name of the server and the DMV (along with
//    the extension. This information is extracted from the  path and an 
//    appropriate SQL query is sent to the required server. The response of 
//    the SQL Query is saved into the file.
//
//    path - relative path from the mount directory
//
// Returns:
//    0 on success, 
//    -errno if a system call failed,
//    -1 on internal error.
//
static int
GetDmvFileContent(
    string path)
{
    int                 error = 0;
    vector<string>      tokens;
    string              filename;
    string              query;
    string              servername;
    string              hostname;
    string              username;
    string              password;
    enum FileFormat     type;
    string              responseString;
    string              tempString1;
    string              tempString2;
    string              dumpPath;
    int                 fd;

    dumpPath = CalculateDumpPath(path);

    // Open the file.
    //
    fd = open(dumpPath.c_str(), O_WRONLY);
    if (fd != -1)
    {
        // Extract SQL server name, DMV name and type
        // Tokenising the path.
        //
        tokens = Split(path, '/');

        // path is of the form <servername>/<filename>
        // On success, there will be more than 1 token.
        //
        assert(tokens.size() > 1);

        servername = tokens[0];
        filename = tokens[1];

        // Now we have the filename - check if it's a JSON
        // We can also check from version but need to extract the
        // the file name in any case.
        //
        size_t found = filename.find(".json");

        if (found != string::npos)
        {
            // Removing the .json from the filename.
            //
            filename = filename.substr(0, found);
            type = TYPE_JSON;
            tempString1 = "SELECT * FROM [master].[sys].[";
            tempString2 = "] FOR JSON AUTO, ROOT('info')";
        }
        else
        {
            type = TYPE_TSV;
            tempString1 = "SELECT * FROM [master].[sys].[";
            tempString2 = "]";
        }

        query = tempString1 + filename + tempString2;

        // Fetch the details for the server.
        //
        GetServerDetails(servername, hostname, username, password);

        error = ExecuteQuery(query, responseString, hostname,
                                username, password, type);

        if (!error)
        {
            // File was already opened and it's file descriptor saved for use.
            //
            if (pwrite(fd, responseString.c_str(), responseString.length(), 0) == -1)
            {
                error = ReturnErrnoAndPrintError(__FUNCTION__, "pwrite failed");
            }
        }
        else
        {
            PrintMsg("Querying the SQL failed. error = %d\n", error);
        }
        
        close(fd);
    }
    else
    {
        error = ReturnErrnoAndPrintError(__FUNCTION__, "open failed");
    }


    return error;
}

// ---------------------------------------------------------------------------
// Method: OpenLocalImpl
//
// Description:
//    This method implements the open system call in the following manner:
//    1. It will redirect the open system call to the dump directory and save
//       file description in the fuse_file_info pointer passed in.
//    2. If this is a DMV - it will also query the server for the content.
//    3. If this is a custom query file, it will run the query and save
//       the result to the output file in the dump directory
//
// Returns:
//    0 on success, 
//    -errno if a system call failed,
//    -1 on internal error.
//
static int
OpenLocalImpl(
    const char* path,
    struct fuse_file_info* fi)
{
    int error = 0;
    int fd;
    string fpath;
    vector<string>      tokens;
    string servername;
    string filename;
    string userQueriesPath;
    string queryFilePath;
    ServerInfo* serverInfo;

    fpath = CalculateDumpPath(path);
    // Open the file.
    //
    fd = open(fpath.c_str(), fi->flags);
    if (fd == -1)
    {
        error = ReturnErrnoAndPrintError(__FUNCTION__, "open failed");
    }
    else
    {
        // Save fd for later use.
        //
        fi->fh = fd;
    }

    if (!error)
    {
        // For dbfs file, fetch the content.
        //
        if (IsDbfsFile(path))
        {
            if (strstr(path, CUSTOM_QUERY_FOLDER_NAME))
            {
                // Tokenising the path.
                //
                tokens = Split(path, '/');
                
                // Path is of the form <servername>/<customQueries>/<filename>
                // On success, there will be more than 1 token.
                //
                assert(tokens.size() > 1);
            
                servername = tokens[0];
                filename = tokens[2];
                assert(strcmp(tokens[1].c_str(), CUSTOM_QUERY_FOLDER_NAME) == 0);

                // Get the path to the custom query directory user specified.
                //
                serverInfo = GetServerInfo(servername);
                if (serverInfo)
                {
                    userQueriesPath = serverInfo->m_customQueriesPath;

                    if (!userQueriesPath.empty())
                    {
                        // Construct the full path name to the query file
                        //
                        queryFilePath = StringFormat("%s/%s", userQueriesPath.c_str(), filename.c_str());

                        // Execute the custom query and put the output to the output file
                        // in the dump directory.
                        //
                        ExecuteCustomQuery(
                            queryFilePath,
                            fpath,
                            serverInfo->m_hostname, 
                            serverInfo->m_username,
                            serverInfo->m_password);
                    }
                }
            }
            else
            {
                error = GetDmvFileContent(path);
            }
        }
    }

    if (error && fd != -1)
    {
        close(fd);
        fi->fh = -1;
    }

    return error;
}

// ---------------------------------------------------------------------------
// Method: ReadLocalImpl
//
// Description:
//    This method redirects the read system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
ReadLocalImpl(
    const char* path,
    char* buf,
    size_t size,
    off_t offset,
    struct fuse_file_info* fi)
{
    int fd = 0;
    int result = -1;

    // Get file descriptor
    //
    GetFileDescriptorForPath(path, fi, fd);

    if (fd != -1)
    {
        result = pread(fd, buf, size, offset);
        if (result == -1)
        {
            result = ReturnErrnoAndPrintError(__FUNCTION__, "pread failed");
        }

        CloseFileDesciptorIfOpened(fi, fd);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: WriteLocalImpl
//
// Description:
//    This method redirects the write system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
WriteLocalImpl(
    const char* path,
    const char* buf,
    size_t size,
    off_t offset,
    struct fuse_file_info* fi)
{
    int     fd;
    string  fpath;
    int     result = 0;

    if (!IsDbfsFile(path))
    {
        GetFileDescriptorForPath(path, fi, fd);

        if (fd != -1)
        {
            result = pwrite(fd, buf, size, offset);
            if (result == -1)
            {
                result = ReturnErrnoAndPrintError(__FUNCTION__, "pwrite failed");
            }

            CloseFileDesciptorIfOpened(fi, fd);
        }
    }
    else
    {
        // It is not permitted to write to the dbfs file.
        //
        PrintMsg("Cannot write to the dbfs files.\n");
        result = -EPERM;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: StatfsLocalImpl
//
// Description:
//    This method redirects the statfs system call
//    the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
StatfsLocalImpl(
    const char* path,
    struct statvfs* stbuf)
{
    int     result;
    string  fpath;

    fpath = CalculateDumpPath(path);
    result = statvfs(fpath.c_str(), stbuf);
    if (result == -1)
    {
        result = ReturnErrnoAndPrintError(__FUNCTION__, "statvfs failed");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: ReleaseLocalImpl
//
// Description:
//    If the file is a DMV file - this method truncates an open DMV file 
//    back to size 0. This effectively removes all the data that was 
//    fetched from the server on open().
//
//    In all cases - close the file descriptor.
//
// Returns:
//    0 on success and -errno on error.
//
static int
ReleaseLocalImpl(
    const char* path,
    struct fuse_file_info* fi)
{
    int result = 0;

    if (IsDbfsFile(path))
    {
        // Reset the DMV file.
        //
        result = TruncateLocalImpl(path, 0);
        if (result == -1)
        {
            result = ReturnErrnoAndPrintError(__FUNCTION__,
                                              "TruncateLocalImpl failed");
        }
    }

    result = close(fi->fh);
    if (result == -1)
    {
        result = ReturnErrnoAndPrintError(__FUNCTION__,
                                          "close failed");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: FsyncLocalImpl
//
// Description:
//    Just a stub. 
//    This method is optional and can safely be left unimplemented.
//
// Returns:
//    0
//
static int
FsyncLocalImpl(
    const char* path,
    int isdatasync,
    struct fuse_file_info* fi)
{
    (void)path;
    (void)isdatasync;
    (void)fi;
    fprintf(stderr, "%s NOT IMPLEMENTED ****\n", __FUNCTION__);
    return 0;
}

// ---------------------------------------------------------------------------
// Method: FallocateLocalImpl
//
// Description:
//    This method redirects the fallocate system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
FallocateLocalImpl(
    const char* path,
    int mode,
    off_t offset,
    off_t length,
    struct fuse_file_info* fi)
{
    int     fd;
    string  fpath;
    int     result = 0;

    GetFileDescriptorForPath(path, fi, fd);

    if (fd != -1)
    {
        if (mode)
        {
            result = -EOPNOTSUPP;
        }
        else
        {
            result = -posix_fallocate(fd, offset, length);
        }

        CloseFileDesciptorIfOpened(fi, fd);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: SetxattrLocalImpl
//
// Description:
//    This method redirects the setxattr system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
SetxattrLocalImpl(
    const char* path,
    const char* name,
    const char* value,
    size_t size, 
    int flags)
{
    int     result;
    string  fpath;

    fpath = CalculateDumpPath(path);
    result = lsetxattr(fpath.c_str(), name, value, size, flags);
    if (result == -1)
    {
        result = -errno;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: GetxattrLocalImpl
//
// Description:
//    This method redirects the getxattr system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
GetxattrLocalImpl(
    const char* path,
    const char* name,
    char* value,
    size_t size)
{
    int     result;
    string  fpath;

    fpath = CalculateDumpPath(path);
    result = lgetxattr(fpath.c_str(), name, value, size);
    if (result == -1)
    {
        result = -errno;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: ListxattrLocalImpl
//
// Description:
//    This method redirects the listxattr system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
ListxattrLocalImpl(
    const char* path,
    char* list,
    size_t size)
{
    int     result;
    string  fpath;

    fpath = CalculateDumpPath(path);
    result = llistxattr(fpath.c_str(), list, size);
    if (result == -1)
    {
        result = -errno;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: RemovexattrLocalImpl
//
// Description:
//    This method redirects the removexattr system call to the dump directory.
//
// Returns:
//    0 on success and -errno on error.
//
static int
RemovexattrLocalImpl(
    const char* path,
    const char* name)
{
    int     result;
    string  fpath;

    fpath = CalculateDumpPath(path);
    result = lremovexattr(fpath.c_str(), name);
    if (result == -1)
    {
        result = -errno;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Method: InitializeSQLFs
//
// Description:
//    This method gets invoked as the first step in FUSE setup.
//    It mainly creates the dump directory (if one is not already present) 
//    and creates the DMV's for all the servers. 
//
// Returns:
//    NULL
//
void*
InitializeSQLFs(
    fuse_conn_info* conn)
{
    int                 result;
    class ServerInfo*   entry;

    (void)conn;

    // Creating the dump dir.
    //
    result = mkdir(g_UserPaths.m_dumpPath.c_str(), DEFAULT_PERMISSIONS);
    if (result == -1)
    {
        PrintMsg("Mkdir failed for %s - %s\n", 
            g_UserPaths.m_dumpPath.c_str(), strerror(errno));
        KillSelf();
    }

    // Create local DMV entries for all the servers.
    //
    for (auto&& itr : g_ServerInfoMap)
    {
        entry = itr.second;
        CreateDbfsFiles(itr.first,
            entry->m_hostname, 
            entry->m_username,
            entry->m_password, 
            entry->m_version);
    }

    return nullptr;
}

// ---------------------------------------------------------------------------
// Method: DestroySQLFs
//
// Description:
//    This method gets invoked if and when FUSE instance is closing. 
//    It deletes the dump directory and all the DMV's created in it.
//
// Returns:
//    VOID
//
void
DestroySQLFs(void* userdata)
{
    PrintMsg("Closing SQLFS\n");
}

// ---------------------------------------------------------------------------
// Structure to map system calls to user level functions for the mount
// directory.
//
static void
InitializeFuseOperations(
    struct fuse_operations* sqlFsOperations)
{
    sqlFsOperations->getattr = GetattrLocalImpl;
    sqlFsOperations->readlink = ReadlinkLocalImpl;
    sqlFsOperations->getdir = NULL;
    sqlFsOperations->mknod = MknodLocalImpl;
    sqlFsOperations->mkdir = MkdirLocalImpl;
    sqlFsOperations->unlink = UnlinkLocalImpl;
    sqlFsOperations->rmdir = RmdirLocalImpl;
    sqlFsOperations->symlink = SymlinkLocalImpl;
    sqlFsOperations->rename = RenameLocalImpl;
    sqlFsOperations->link = LinkLocalImpl;
    sqlFsOperations->chmod = ChmodLocalImpl;
    sqlFsOperations->chown = ChownLocalImpl;
    sqlFsOperations->truncate = TruncateLocalImpl;
    sqlFsOperations->utime = NULL;
    sqlFsOperations->open = OpenLocalImpl;
    sqlFsOperations->read = ReadLocalImpl;
    sqlFsOperations->write = WriteLocalImpl;
    sqlFsOperations->statfs = StatfsLocalImpl;
    sqlFsOperations->flush = NULL;
    sqlFsOperations->release = ReleaseLocalImpl;
    sqlFsOperations->fsync = FsyncLocalImpl;
    sqlFsOperations->setxattr = SetxattrLocalImpl;
    sqlFsOperations->getxattr = GetxattrLocalImpl;
    sqlFsOperations->listxattr = ListxattrLocalImpl;
    sqlFsOperations->removexattr = RemovexattrLocalImpl;
    sqlFsOperations->opendir = OpendirLocalImpl;
    sqlFsOperations->readdir = ReaddirLocalImpl;
    sqlFsOperations->releasedir = ReleasedirLocalImpl;
    sqlFsOperations->fsyncdir = NULL;
    sqlFsOperations->init = InitializeSQLFs;
    sqlFsOperations->destroy = DestroySQLFs;
    sqlFsOperations->access = AccessLocalImpl;
    sqlFsOperations->create = NULL;
    sqlFsOperations->ftruncate = NULL;
    sqlFsOperations->fgetattr = NULL;
    sqlFsOperations->lock = NULL;
    sqlFsOperations->utimens = UtimensLocalImpl;
    sqlFsOperations->bmap = NULL;
    sqlFsOperations->flag_nullpath_ok = 0;
    sqlFsOperations->flag_nopath = 0;
    sqlFsOperations->flag_utime_omit_ok = 0;
    sqlFsOperations->flag_reserved = 0;
    sqlFsOperations->ioctl = NULL;
    sqlFsOperations->poll = NULL;
    sqlFsOperations->write_buf = NULL;
    sqlFsOperations->read_buf = NULL;
    sqlFsOperations->flock = NULL;
    sqlFsOperations->fallocate = FallocateLocalImpl;
}

// ---------------------------------------------------------------------------
// Method: StartFuse
//
// Description:
//    This method starts the fuse instance for the given mount point.
//    argc and argv need to be constructed to cater to the arguments 
//    fuse_main() expects.
//
//    Options -o and direct_io are passed because because before a read()
//    kernel does a query to get the size of the file but that returns zero 
//    because only at open do we put data in the file. So this doesn't work
//    well when kernel is using its cache (direct_io option disables that).
//
// Returns:
//    VOID
//
int
StartFuse(
    char* ProgramName)
{
    int                     argc;
    char*                   argv[MAX_ARGS];
    struct fuse_operations  sqlFsOperations;
    char*                   buffer;
    int                     result;
    string                  tdsString;
    char                    varArray[24];
    
    InitializeFuseOperations(&sqlFsOperations);

    // Set the TDS version.
    //
    tdsString = "TDSVER=8.0";
    strcpy(varArray, tdsString.c_str());
    result = putenv(varArray);
    assert(!result);
    
    // Setup argc and argv for fuse.
    //
    memset(argv, 0, sizeof(argv));
    argc = 0;
    argv[argc++] = ProgramName;

    buffer = strdup(g_UserPaths.m_mountPath.c_str());
    assert(buffer);
    argv[argc++] = buffer;
    
    if (g_RunInForeground)
    {
        buffer = strdup("-f");
        assert(buffer);
        argv[argc++] = buffer;
    }

    buffer = strdup("-o");
    assert(buffer);
    argv[argc++] = buffer;

    buffer = strdup("direct_io");
    assert(buffer);
    argv[argc++] = buffer;

    PrintMsg("Starting fuse\n");

    result = fuse_main(argc, argv, &sqlFsOperations, NULL);

    // Free space consumed by argv 
    // argv[0] is not assigned by us so don't free it.
    //
    while (argc > 1)
    {
        free(argv[--argc]);
    }

    return result;
}
