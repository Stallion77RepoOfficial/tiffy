#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tigre.h"

static void put_u16_le(unsigned char *p, uint16_t v){
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void put_u64_le(unsigned char *p, uint64_t v){
    for (int i=0;i<8;i++){
        p[i] = (unsigned char)((v >> (8*i)) & 0xFF);
    }
}

int main(void){
    char tmpl[] = "ifd_overflowXXXXXX";
    int fd = mkstemp(tmpl);
    assert(fd >= 0);
    unlink(tmpl);

    unsigned char count_buf[8];
    put_u64_le(count_buf, 2);
    assert(pwrite(fd, count_buf, sizeof(count_buf), 0) == (ssize_t)sizeof(count_buf));

    unsigned char entries[40];
    memset(entries, 0, sizeof(entries));

    uint64_t huge_count = (UINT64_MAX / 8ULL) + 1ULL;

    put_u16_le(entries + 0, 273);      // StripOffsets tag
    put_u16_le(entries + 2, 16);       // LONG8 type
    put_u64_le(entries + 4, huge_count);
    put_u64_le(entries + 12, 0);       // value/offset (unused in this test)

    put_u16_le(entries + 20, 279);     // StripByteCounts tag
    put_u16_le(entries + 22, 16);
    put_u64_le(entries + 24, 1);
    put_u64_le(entries + 32, 0);

    assert(pwrite(fd, entries, sizeof(entries), 8) == (ssize_t)sizeof(entries));

    tig_tiff_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.is_big_tiff = 1;
    hdr.is_le = 1;
    hdr.hdr_off = 0;
    hdr.first_ifd = 0;

    tig_tiff_ifd ifd;
    errno = 0;
    int rc = tig_parse_ifd(fd, &hdr, &ifd);
    assert(rc != 0);
    assert(errno == ENOMEM);

    close(fd);
    return 0;
}
