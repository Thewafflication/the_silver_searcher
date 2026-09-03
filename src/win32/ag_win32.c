/*
 * Windows implementations of the POSIX interfaces declared in ag_win32.h.
 *
 * This is the only ag translation unit that sees <windows.h>. Everything here
 * is limited to APIs present in Windows 2000, matching WCRT's floor.
 */

#include "ag_win32.h"
#include "fcntl.h"

/* This file defines the real main(), so ag's rename must not apply here. */
#undef main

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

int ag_main(int argc, char **argv);

/*
 * Descriptors start well above WCRT's FILE descriptors (3 through FOPEN_MAX+3)
 * so a stream descriptor can never be mistaken for one of ours.
 */
#define AG_DESCRIPTOR_BASE 4096
#define AG_DESCRIPTOR_COUNT 512
#define AG_PIPE_COUNT 16

static HANDLE ag_descriptors[AG_DESCRIPTOR_COUNT];
static CRITICAL_SECTION ag_descriptor_lock;

static struct {
    FILE *stream;
    char path[MAX_PATH + 1];
} ag_pipes[AG_PIPE_COUNT];
static CRITICAL_SECTION ag_pipe_lock;

static CRITICAL_SECTION ag_global_lock;

static int ag_started;

void ag_win32_startup(void) {
    if (ag_started) {
        return;
    }
    ag_started = 1;
    memset(ag_descriptors, 0, sizeof(ag_descriptors));
    memset(ag_pipes, 0, sizeof(ag_pipes));
    InitializeCriticalSection(&ag_descriptor_lock);
    InitializeCriticalSection(&ag_pipe_lock);
    InitializeCriticalSection(&ag_global_lock);
}

void ag_win32_global_lock(void) {
    EnterCriticalSection(&ag_global_lock);
}

void ag_win32_global_unlock(void) {
    LeaveCriticalSection(&ag_global_lock);
}

int main(int argc, char **argv) {
    ag_win32_startup();
    return ag_main(argc, argv);
}

/* Maps the Win32 status codes this layer can actually produce. */
static void ag_set_errno(DWORD status) {
    switch (status) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_NAME:
        case ERROR_BAD_NETPATH:
            errno = ENOENT;
            break;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            errno = EACCES;
            break;
        case ERROR_FILE_EXISTS:
        case ERROR_ALREADY_EXISTS:
            errno = EEXIST;
            break;
        case ERROR_TOO_MANY_OPEN_FILES:
            errno = EMFILE;
            break;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            errno = ENOMEM;
            break;
        case ERROR_INVALID_HANDLE:
        case ERROR_INVALID_PARAMETER:
            errno = EINVAL;
            break;
        default:
            errno = EIO;
            break;
    }
}

static HANDLE ag_standard_handle(int descriptor) {
    switch (descriptor) {
        case 0:
            return GetStdHandle(STD_INPUT_HANDLE);
        case 1:
            return GetStdHandle(STD_OUTPUT_HANDLE);
        case 2:
            return GetStdHandle(STD_ERROR_HANDLE);
        default:
            return INVALID_HANDLE_VALUE;
    }
}

static HANDLE ag_handle(int descriptor) {
    int slot;
    HANDLE handle;

    if (descriptor >= 0 && descriptor <= 2) {
        return ag_standard_handle(descriptor);
    }
    slot = descriptor - AG_DESCRIPTOR_BASE;
    if (slot < 0 || slot >= AG_DESCRIPTOR_COUNT) {
        return INVALID_HANDLE_VALUE;
    }
    EnterCriticalSection(&ag_descriptor_lock);
    handle = ag_descriptors[slot];
    LeaveCriticalSection(&ag_descriptor_lock);
    return handle == NULL ? INVALID_HANDLE_VALUE : handle;
}

void *_get_osfhandle(int descriptor) {
    return ag_handle(descriptor);
}

int fileno(FILE *stream) {
    return _fileno(stream);
}

int open(const char *path, int flags, ...) {
    DWORD access = GENERIC_READ;
    DWORD disposition = OPEN_EXISTING;
    HANDLE handle;
    int slot;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    if ((flags & O_WRONLY) != 0) {
        access = GENERIC_WRITE;
    } else if ((flags & O_RDWR) != 0) {
        access = GENERIC_READ | GENERIC_WRITE;
    }
    if ((flags & O_CREAT) != 0) {
        if ((flags & O_EXCL) != 0) {
            disposition = CREATE_NEW;
        } else if ((flags & O_TRUNC) != 0) {
            disposition = CREATE_ALWAYS;
        } else {
            disposition = OPEN_ALWAYS;
        }
    } else if ((flags & O_TRUNC) != 0) {
        disposition = TRUNCATE_EXISTING;
    }

    /*
     * ag reads whole files front to back, and must not stop another process
     * from writing to or replacing a file while it is being searched.
     */
    handle = CreateFileA(path, access,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        disposition, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        ag_set_errno(GetLastError());
        return -1;
    }

    EnterCriticalSection(&ag_descriptor_lock);
    for (slot = 0; slot < AG_DESCRIPTOR_COUNT; slot++) {
        if (ag_descriptors[slot] == NULL) {
            ag_descriptors[slot] = handle;
            break;
        }
    }
    LeaveCriticalSection(&ag_descriptor_lock);

    if (slot == AG_DESCRIPTOR_COUNT) {
        CloseHandle(handle);
        errno = EMFILE;
        return -1;
    }
    return slot + AG_DESCRIPTOR_BASE;
}

int close(int descriptor) {
    int slot = descriptor - AG_DESCRIPTOR_BASE;
    HANDLE handle = NULL;

    if (slot < 0 || slot >= AG_DESCRIPTOR_COUNT) {
        errno = EINVAL;
        return -1;
    }
    EnterCriticalSection(&ag_descriptor_lock);
    handle = ag_descriptors[slot];
    ag_descriptors[slot] = NULL;
    LeaveCriticalSection(&ag_descriptor_lock);

    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (!CloseHandle(handle)) {
        ag_set_errno(GetLastError());
        return -1;
    }
    return 0;
}

ssize_t read(int descriptor, void *buffer, size_t count) {
    HANDLE handle = ag_handle(descriptor);
    DWORD transferred = 0;
    DWORD request;

    if (handle == INVALID_HANDLE_VALUE) {
        errno = EINVAL;
        return -1;
    }
    /* One ReadFile cannot exceed a DWORD; a short read is a valid result. */
    request = count > 0x7FFFFFFFU ? 0x7FFFFFFFU : (DWORD)count;
    if (!ReadFile(handle, buffer, request, &transferred, NULL)) {
        DWORD status = GetLastError();
        /* A closed pipe reads as end of file, not as an error. */
        if (status == ERROR_BROKEN_PIPE || status == ERROR_HANDLE_EOF) {
            return 0;
        }
        ag_set_errno(status);
        return -1;
    }
    return (ssize_t)transferred;
}

/* Converts a Windows FILETIME to whole seconds since the Unix epoch. */
static time_t ag_unix_time(const FILETIME *value) {
    unsigned long long ticks =
        ((unsigned long long)value->dwHighDateTime << 32) | value->dwLowDateTime;
    if (ticks < 116444736000000000ULL) {
        return (time_t)0;
    }
    return (time_t)((ticks - 116444736000000000ULL) / 10000000ULL);
}

static void ag_fill_time(const FILETIME *value, struct timespec *high_resolution,
    time_t *seconds) {
    time_t converted = ag_unix_time(value);
    *seconds = converted;
    high_resolution->tv_sec = converted;
    high_resolution->tv_nsec = 0;
}

int fstat(int descriptor, struct stat *result) {
    HANDLE handle = ag_handle(descriptor);
    BY_HANDLE_FILE_INFORMATION information;
    DWORD type;

    if (result == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (handle == INVALID_HANDLE_VALUE) {
        errno = EINVAL;
        return -1;
    }
    memset(result, 0, sizeof(*result));
    result->st_nlink = 1;

    type = GetFileType(handle);
    if (type == FILE_TYPE_PIPE) {
        result->st_mode = S_IFIFO | S_IRUSR | S_IWUSR;
        return 0;
    }
    if (type == FILE_TYPE_CHAR) {
        result->st_mode = S_IFCHR | S_IRUSR | S_IWUSR;
        return 0;
    }
    if (type != FILE_TYPE_DISK) {
        errno = EINVAL;
        return -1;
    }

    if (!GetFileInformationByHandle(handle, &information)) {
        ag_set_errno(GetLastError());
        return -1;
    }

    if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        result->st_mode = S_IFDIR | S_IRUSR | S_IXUSR;
    } else {
        result->st_mode = S_IFREG | S_IRUSR;
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_READONLY) == 0) {
        result->st_mode |= S_IWUSR;
    }
    result->st_dev = (dev_t)information.dwVolumeSerialNumber;
    result->st_rdev = result->st_dev;
    result->st_nlink = (nlink_t)information.nNumberOfLinks;
    result->st_size = (off_t)(((unsigned long long)information.nFileSizeHigh << 32) |
        information.nFileSizeLow);
    /*
     * st_ino stays zero to match WCRT's stat(). ag compares the two against
     * each other to avoid searching the file its own output is redirected to,
     * and a shared zero disables that check rather than making every file
     * look like the output file.
     */
    result->st_ino = 0;
    ag_fill_time(&information.ftLastAccessTime, &result->st_atim, &result->st_atime);
    ag_fill_time(&information.ftLastWriteTime, &result->st_mtim, &result->st_mtime);
    ag_fill_time(&information.ftCreationTime, &result->st_ctim, &result->st_ctime);
    return 0;
}

int isatty(int descriptor) {
    HANDLE handle = ag_handle(descriptor);
    DWORD mode;

    if (handle == INVALID_HANDLE_VALUE) {
        return 0;
    }
    return GetConsoleMode(handle, &mode) ? 1 : 0;
}

char *realpath(const char *path, char *resolved) {
    char buffer[MAX_PATH + 1];
    DWORD length;

    if (path == NULL) {
        errno = EINVAL;
        return NULL;
    }
    /* GetFullPathNameA normalizes forward slashes and relative components. */
    length = GetFullPathNameA(path, MAX_PATH + 1, buffer, NULL);
    if (length == 0 || length > MAX_PATH) {
        ag_set_errno(length == 0 ? GetLastError() : ERROR_INVALID_NAME);
        return NULL;
    }
    if (resolved != NULL) {
        memcpy(resolved, buffer, (size_t)length + 1);
        return resolved;
    }
    resolved = (char *)malloc((size_t)length + 1);
    if (resolved == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    memcpy(resolved, buffer, (size_t)length + 1);
    return resolved;
}

int gettimeofday(struct timeval *now, void *timezone_unused) {
    FILETIME value;
    unsigned long long ticks;

    (void)timezone_unused;
    if (now == NULL) {
        errno = EINVAL;
        return -1;
    }
    GetSystemTimeAsFileTime(&value);
    ticks = ((unsigned long long)value.dwHighDateTime << 32) | value.dwLowDateTime;
    if (ticks < 116444736000000000ULL) {
        ticks = 116444736000000000ULL;
    }
    ticks -= 116444736000000000ULL;
    now->tv_sec = (long)(ticks / 10000000ULL);
    now->tv_usec = (long)((ticks % 10000000ULL) / 10ULL);
    return 0;
}

/*
 * popen()/pclose().
 *
 * The child writes to a temporary file which is then opened for reading, so
 * the caller gets a real WCRT stream without WCRT needing an fdopen().
 */
FILE *popen(const char *command, const char *mode) {
    char directory[MAX_PATH + 1];
    char path[MAX_PATH + 1];
    char *command_line = NULL;
    SECURITY_ATTRIBUTES inheritable;
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    HANDLE output = INVALID_HANDLE_VALUE;
    HANDLE null_device = INVALID_HANDLE_VALUE;
    FILE *stream = NULL;
    size_t length;
    int slot;

    if (command == NULL || mode == NULL || mode[0] != 'r') {
        errno = EINVAL;
        return NULL;
    }
    if (GetTempPathA(MAX_PATH + 1, directory) == 0 ||
        GetTempFileNameA(directory, "ag", 0, path) == 0) {
        ag_set_errno(GetLastError());
        return NULL;
    }

    inheritable.nLength = sizeof(inheritable);
    inheritable.lpSecurityDescriptor = NULL;
    inheritable.bInheritHandle = TRUE;

    output = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, &inheritable,
        CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (output == INVALID_HANDLE_VALUE) {
        ag_set_errno(GetLastError());
        DeleteFileA(path);
        return NULL;
    }
    null_device = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);

    /* cmd.exe is needed because ag's commands contain shell redirections. */
    length = strlen(command) + 32;
    command_line = (char *)malloc(length);
    if (command_line == NULL) {
        errno = ENOMEM;
        goto failed;
    }
    _snprintf(command_line, length, "cmd.exe /S /C \"%s\"", command);
    command_line[length - 1] = '\0';

    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = null_device;
    startup.hStdOutput = output;
    startup.hStdError = null_device;
    memset(&process, 0, sizeof(process));

    if (!CreateProcessA(NULL, command_line, NULL, NULL, TRUE, CREATE_NO_WINDOW,
            NULL, NULL, &startup, &process)) {
        ag_set_errno(GetLastError());
        goto failed;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    free(command_line);
    command_line = NULL;
    CloseHandle(output);
    output = INVALID_HANDLE_VALUE;
    if (null_device != INVALID_HANDLE_VALUE) {
        CloseHandle(null_device);
        null_device = INVALID_HANDLE_VALUE;
    }

    stream = fopen(path, "rb");
    if (stream == NULL) {
        DeleteFileA(path);
        return NULL;
    }

    EnterCriticalSection(&ag_pipe_lock);
    for (slot = 0; slot < AG_PIPE_COUNT; slot++) {
        if (ag_pipes[slot].stream == NULL) {
            ag_pipes[slot].stream = stream;
            strcpy(ag_pipes[slot].path, path);
            break;
        }
    }
    LeaveCriticalSection(&ag_pipe_lock);

    if (slot == AG_PIPE_COUNT) {
        /* Nothing left to record the temporary file in; do not leak it. */
        fclose(stream);
        DeleteFileA(path);
        errno = EMFILE;
        return NULL;
    }
    return stream;

failed:
    free(command_line);
    if (output != INVALID_HANDLE_VALUE) {
        CloseHandle(output);
    }
    if (null_device != INVALID_HANDLE_VALUE) {
        CloseHandle(null_device);
    }
    DeleteFileA(path);
    return NULL;
}

int pclose(FILE *stream) {
    char path[MAX_PATH + 1];
    int slot;
    int found = 0;

    if (stream == NULL) {
        errno = EINVAL;
        return -1;
    }
    EnterCriticalSection(&ag_pipe_lock);
    for (slot = 0; slot < AG_PIPE_COUNT; slot++) {
        if (ag_pipes[slot].stream == stream) {
            strcpy(path, ag_pipes[slot].path);
            ag_pipes[slot].stream = NULL;
            ag_pipes[slot].path[0] = '\0';
            found = 1;
            break;
        }
    }
    LeaveCriticalSection(&ag_pipe_lock);

    fclose(stream);
    if (!found) {
        errno = EINVAL;
        return -1;
    }
    DeleteFileA(path);
    return 0;
}
