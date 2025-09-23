#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#endif

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
static int write_full(int fd, const void *buf, size_t n){
    size_t done = 0;
    while (done < n){
        int w = _write(fd, (const char*)buf + done, (unsigned)(n - done));
        if (w <= 0) return -1;
        done += (size_t)w;
    }
    return 0;
}
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <fcntl.h>
#  include <unistd.h>
static int open_out(const char* p){
    int fd = open(p, O_CREAT|O_TRUNC|O_WRONLY, 0644);
    return fd<0?-1:fd;
}
static int write_full(int fd, const void *buf, size_t n){
    size_t done = 0;
    while (done < n){
        ssize_t w = write(fd, (const char*)buf + done, n - done);
        if (w < 0){
            if (errno == EINTR) continue;
            return -1;
        }
        if (w == 0) return -1;
        done += (size_t)w;
    }
    return 0;
}
#endif

static int pread_all(int fd, void *buf, size_t n, uint64_t off, size_t *out_got){
#if defined(_WIN32)
    if (_lseeki64(fd, off, SEEK_SET) < 0) return -1;
    size_t got=0;
    while (got<n){
        int r = _read(fd, (char*)buf+got, (unsigned)(n-got));
        if (r<0) return -2;
        if (r==0) break;
        got += (size_t)r;
    }
    if (out_got) *out_got = got;
    return 0;
#else
    size_t got=0;
    while (got<n){
        ssize_t r = pread(fd, (char*)buf+got, n-got, (off_t)off+got);
        if (r<0) return -2;
        if (r==0) break;
        got += (size_t)r;
    }
    if (out_got) *out_got = got;
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
    int map_has_entry = 0;
    if (map_json && *map_json){
        map = fopen(map_json,"w");
        if (map){
            fprintf(map,"[\n");
        } else {
            fprintf(stderr,"[clone] map dosyası açılamadı: %s\n", map_json);
        }
    }
    const uint64_t progress_interval = 100; // report every 100 blocks

    while (1){
        size_t got = 0;
        int r = pread_all(sfd, buf, BS, off, &got);
        if (r!=0){
            int ok=0;
            for (int t=0;t<retry;t++){
                r = pread_all(sfd, buf, BS, off, &got);
                if (r==0){ ok=1; break; }
            }
            if (!ok){
                if (skip_on_error){
                    // delik: hedefte sıfır yaz
                    void *zb = calloc(1, BS);
                    if (write_full(dfd, zb, BS)!=0){
                        fprintf(stderr,"[clone] yazma hatası: %s\n", strerror(errno));
                        free(zb);
                        err=5;
                        break;
                    }
                    if (map){
                        if (map_has_entry) fprintf(map,",\n");
                        fprintf(map,"  {\"off\": %llu, \"len\": %zu, \"status\": \"hole\"}",
                                (unsigned long long)off, BS);
                        map_has_entry = 1;
                    }
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
        if (got==0){
            // EOF reached cleanly
            break;
        }
        size_t to_write = got;
        // başarı: bloğu yaz
#if defined(_WIN32)
        if (write_full(dfd, buf, to_write)!=0){
            fprintf(stderr,"[clone] yazma hatası: %s\n", strerror(errno));
            err=5; break;
        }
#else
        // If the block is entirely zero, create a sparse hole by seeking instead of writing
        int all_zero = 1;
        for (size_t zi = 0; zi < to_write; zi++){
            if (((unsigned char*)buf)[zi] != 0){ all_zero = 0; break; }
        }
        if (all_zero){
            off_t cur = lseek(dfd, 0, SEEK_CUR);
            if (cur == (off_t)-1){
                // lseek not supported on this fd? fallback to writing
                if (write_full(dfd, buf, to_write)!=0){
                    fprintf(stderr,"[clone] yazma hatası: %s\n", strerror(errno));
                    err=5; break;
                }
            } else {
                // advance file pointer creating a hole
                if (lseek(dfd, (off_t)to_write, SEEK_CUR) == (off_t)-1){
                    // if seek fails, fallback to writing
                    if (write_full(dfd, buf, to_write)!=0){
                        fprintf(stderr,"[clone] yazma hatası: %s\n", strerror(errno));
                        err=5; break;
                    }
                }
            }
            if (map){
                if (map_has_entry) fprintf(map,",\n");
                fprintf(map,"  {\"off\": %llu, \"len\": %zu, \"status\": \"hole\"}",
                        (unsigned long long)off, to_write);
                map_has_entry = 1;
            }
        } else {
            if (write_full(dfd, buf, to_write)!=0){
                fprintf(stderr,"[clone] yazma hatası: %s\n", strerror(errno));
                err=5; break;
            }
            if (map){
                if (map_has_entry) fprintf(map,",\n");
                fprintf(map,"  {\"off\": %llu, \"len\": %zu, \"status\": \"ok\"}",
                        (unsigned long long)off, to_write);
                map_has_entry = 1;
            }
        }
#endif
        off += to_write; copied += to_write;

        if (copied >= BS && (copied/BS) % progress_interval == 0){
            fprintf(stderr,"[clone] copied %llu blocks (%llu bytes)\n",
                    (unsigned long long)(copied/BS), (unsigned long long)copied);
        }

        // burada sonsuz akışı durdurmak için bir üst sınır yoksa read hata verene kadar gider.
        // tipik kullanımda kaynak “file” ise boyutu bilinir; device ise kullanıcı range verir.
        // basit strateji: 4TB gibi bir üst sınır parametreleştirilebilir. Burada atlanıyor.
        if (copied > (1ull<<46)) { fprintf(stderr,"[clone] güvenlik sınırı aşıldı, duruyor\n"); break; }

        if (got < BS){
            // final partial chunk processed
            break;
        }
    }

    if (map){
        if (map_has_entry) fprintf(map,"\n");
        fprintf(map,"]\n");
        fclose(map);
    }
    tig_close(sfd);
#if defined(_WIN32)
    _close(dfd);
#else
    close(dfd);
#endif
    free(buf);
    return err;
}
