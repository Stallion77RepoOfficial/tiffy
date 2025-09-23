#include "tigre.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(_WIN32)
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
/* MinGW may not define _S_IREAD/_S_IWRITE; provide fallbacks */
#  ifndef _S_IREAD
#    ifdef S_IRUSR
#      define _S_IREAD S_IRUSR
#    else
#      define _S_IREAD 0400
#    endif
#  endif
#  ifndef _S_IWRITE
#    ifdef S_IWUSR
#      define _S_IWRITE S_IWUSR
#    else
#      define _S_IWRITE 0200
#    endif
#  endif
static int open_out(const char* p){
    int fd = _open(p, _O_CREAT|_O_TRUNC|_O_WRONLY|_O_BINARY, _S_IREAD|_S_IWRITE);
    return fd<0?-1:fd;
}
static int64_t filesize(int fd) __attribute__((unused));
static int64_t filesize(int fd){
    __int64 s = _lseeki64(fd, 0, SEEK_END);
    _lseeki64(fd, 0, SEEK_SET);
    return s;
}
#else
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <unistd.h>
static int open_out(const char* p){
    int fd = open(p, O_CREAT|O_TRUNC|O_WRONLY, 0644);
    return fd<0?-1:fd;
}
static off_t filesize_fd(int fd){
    off_t cur = lseek(fd, 0, SEEK_CUR);
    off_t end = lseek(fd, 0, SEEK_END);
    lseek(fd, cur, SEEK_SET);
    return end;
}
#endif

static int pread_all(int fd, void *buf, size_t n, uint64_t off){
#if defined(_WIN32)
    if (_lseeki64(fd, off, SEEK_SET) < 0) return -1;
    size_t got=0;
    while (got<n){
        int r = _read(fd, (char*)buf+got, (unsigned)(n-got));
        if (r<=0) return -2;
        got += (size_t)r;
    }
    return 0;
#else
    size_t got=0;
    while (got<n){
        ssize_t r = pread(fd, (char*)buf+got, n-got, (off_t)off+got);
        if (r<=0) return -2;
        got += (size_t)r;
    }
    return 0;
#endif
}

int tig_clone_to_img(const char *src_dev, const char *dst_img,
                     const char *map_json, uint64_t block_size,
                     int retry, int skip_on_error)
{
    int sfd = tig_open_ro(src_dev);
    if (sfd<0){ fprintf(stderr,"[clone] kaynak açılamadı: %s\n", src_dev); return 1; }

    int dfd = open_out(dst_img);
    if (dfd<0){ fprintf(stderr,"[clone] hedef açılamadı: %s\n", dst_img); tig_close(sfd); return 2; }

    // boyutu kestiremiyorsak kopyayı “stream” mantığıyla yaparız:
    const size_t BS = (size_t)(block_size ? block_size : (1<<20));
    void *buf = malloc(BS); if(!buf){ fprintf(stderr,"[clone] bellek yok\n"); tig_close(sfd); return 3; }

    uint64_t off=0; uint64_t copied=0; int err=0;
    FILE *map = NULL;
    if (map_json && *map_json) map = fopen(map_json,"w"), fprintf(map,"[\n");
    const uint64_t progress_interval = 100; // report every 100 blocks

    while (1){
        int r = pread_all(sfd, buf, BS, off);
    if (r!=0){
            int ok=0;
            for (int t=0;t<retry;t++){
                r = pread_all(sfd, buf, BS, off);
                if (r==0){ ok=1; break; }
            }
            if (!ok){
                if (skip_on_error){
                    // delik: hedefte sıfır yaz
                    void *zb = calloc(1, BS);
#if defined(_WIN32)
                    _write(dfd, zb, (unsigned)BS);
#else
                    write(dfd, zb, BS);
#endif
                    if (map) fprintf(map,"  {\"off\": %llu, \"len\": %zu, \"status\": \"hole\"},\n",
                                     (unsigned long long)off, BS);
                    free(zb);
                    off += BS; copied += BS;
                    if ((copied/BS) % progress_interval == 0){
                        fprintf(stderr,"[clone] copied %llu blocks (%llu bytes)\n", (unsigned long long)(copied/BS), (unsigned long long)copied);
                    }
                    continue;
                } else {
                    fprintf(stderr,"[clone] okuma hatası @%llu\n", (unsigned long long)off);
                    err=4; break;
                }
            }
        }
    // başarı: bloğu yaz
#if defined(_WIN32)
    _write(dfd, buf, (unsigned)BS);
#else
    // If the block is entirely zero, create a sparse hole by seeking instead of writing
    int all_zero = 1;
    for (size_t zi = 0; zi < BS; zi++){
        if (((unsigned char*)buf)[zi] != 0){ all_zero = 0; break; }
    }
    if (all_zero){
        off_t cur = lseek(dfd, 0, SEEK_CUR);
        if (cur == (off_t)-1){
        // lseek not supported on this fd? fallback to writing
        write(dfd, buf, BS);
        } else {
        // advance file pointer creating a hole
        if (lseek(dfd, BS, SEEK_CUR) == (off_t)-1){
            // if seek fails, fallback to writing
            write(dfd, buf, BS);
        }
        }
        if (map) fprintf(map,"  {\"off\": %llu, \"len\": %zu, \"status\": \"hole\"},\n",
                 (unsigned long long)off, BS);
    } else {
        write(dfd, buf, BS);
        if (map) fprintf(map,"  {\"off\": %llu, \"len\": %zu, \"status\": \"ok\"},\n",
                 (unsigned long long)off, BS);
    }
#endif
    off += BS; copied += BS;

        // burada sonsuz akışı durdurmak için bir üst sınır yoksa read hata verene kadar gider.
        // tipik kullanımda kaynak “file” ise boyutu bilinir; device ise kullanıcı range verir.
        // basit strateji: 4TB gibi bir üst sınır parametreleştirilebilir. Burada atlanıyor.
        if (copied > (1ull<<46)) { fprintf(stderr,"[clone] güvenlik sınırı aşıldı, duruyor\n"); break; }
    }

    if (map){ fseek(map, -2, SEEK_END); fprintf(map,"\n]\n"); fclose(map); }
    tig_close(sfd);
#if defined(_WIN32)
    _close(dfd);
#else
    close(dfd);
#endif
    free(buf);
    return err;
}
