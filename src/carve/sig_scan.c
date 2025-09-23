#include "tigre.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const unsigned char SIGS[4][4] = {
    {0x49,0x49,0x2A,0x00}, // II*
    {0x4D,0x4D,0x00,0x2A}, // MM*
    {0x49,0x49,0x2B,0x00}, // II + BigTIFF
    {0x4D,0x4D,0x00,0x2B}  // MM + BigTIFF
};

static int pread_chunk(int fd, void *buf, size_t n, uint64_t off);

int tig_find_next_tiff(int fd, uint64_t start_off, tig_tiff_header *out_hdr, uint64_t *next_hint){
    const size_t CH = 1<<20;
    const size_t OVERLAP = 3; // keep 3-byte overlap to avoid missing 4-byte signatures at chunk boundaries
    unsigned char *b = malloc(CH);
    if (!b) return -1;
    uint64_t off = start_off;
    while (1){
        if (pread_chunk(fd, b, CH, off)!=0){
            fprintf(stderr,"[sig_scan] read failed at offset %llu\n", (unsigned long long)off);
            free(b);
            return -2;
        }
        for (size_t i=0;i+4<=CH;i++){
            for (int s=0;s<4;s++){
                if (memcmp(b+i, SIGS[s],4)==0){
                    uint64_t hit = off + i;
                    // header oku
                    unsigned char h[8];
                    memcpy(h, b+i, 4);
                    if (pread_chunk(fd, h+4, 4, hit+4)!=0){ free(b); return -3; }

                    int le = (s==0 || s==2);
                    int big = (s>=2);
                    uint64_t first_ifd = 0;
                    if (!big){
                        uint32_t rel = le ? (uint32_t)(h[4]|(h[5]<<8)|(h[6]<<16)|(h[7]<<24))
                                          : (uint32_t)((h[4]<<24)|(h[5]<<16)|(h[6]<<8)|h[7]);
                        first_ifd = hit + rel;
                    } else {
                        unsigned char bb[8];
                        if (pread_chunk(fd, bb, 8, hit+4)!=0){ free(b); return -4; }
                        uint64_t rel = le? (uint64_t)bb[0] | ((uint64_t)bb[1]<<8) | ((uint64_t)bb[2]<<16) |
                                               ((uint64_t)bb[3]<<24)|((uint64_t)bb[4]<<32)|((uint64_t)bb[5]<<40)|
                                               ((uint64_t)bb[6]<<48)|((uint64_t)bb[7]<<56)
                                             : ((uint64_t)bb[0]<<56)|((uint64_t)bb[1]<<48)|((uint64_t)bb[2]<<40)|
                                               ((uint64_t)bb[3]<<32)|((uint64_t)bb[4]<<24)|((uint64_t)bb[5]<<16)|
                                               ((uint64_t)bb[6]<<8)|bb[7];
                        first_ifd = hit + rel;
                    }
                    out_hdr->is_le = le;
                    out_hdr->is_big_tiff = big;
                    out_hdr->hdr_off = hit;
                    out_hdr->first_ifd = first_ifd;
                    if (next_hint) *next_hint = hit + 4;
                    free(b);
                    return 0;
                }
            }
        }
        if (CH > OVERLAP) off += CH - OVERLAP; else off += CH;
    }
    // not reached
    return -5;
}

// basit pread
#if defined(_WIN32)
#  include <io.h>
#  include <fcntl.h>
static int pread_chunk(int fd, void *buf, size_t n, uint64_t off){
    if (_lseeki64(fd, off, SEEK_SET) < 0) return -1;
    size_t got=0;
    while (got<n){
        int r = _read(fd, (char*)buf+got, (unsigned)(n-got));
        if (r<=0) return -2;
        got += (size_t)r;
    }
    return 0;
}
#else
#  include <unistd.h>
static int pread_chunk(int fd, void *buf, size_t n, uint64_t off){
    size_t got=0;
    while (got<n){
        ssize_t r = pread(fd, (char*)buf+got, n-got, (off_t)off+got);
        if (r<=0) return -2;
        got += (size_t)r;
    }
    return 0;
}
#endif
