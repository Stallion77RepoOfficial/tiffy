#include "tigre.h"
#include <errno.h>
#if defined(_WIN32)
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
int tig_open_ro(const char *path){
    HANDLE h = CreateFileA(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h==INVALID_HANDLE_VALUE) return -1;
    int fd = _open_osfhandle((intptr_t)h, _O_RDONLY | _O_BINARY);
    return fd;
}
void tig_close(int fd){ _close(fd); }
#else
#  include <fcntl.h>
#  include <unistd.h>
int tig_open_ro(const char *path){
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    return fd;
}
void tig_close(int fd){ if (fd>=0) close(fd); }
#endif
