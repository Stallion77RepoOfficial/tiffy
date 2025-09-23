#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE

#include "tigre.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>     // pread()
#include <fcntl.h>
#include <sys/types.h>
#if !defined(_WIN32)
#include <sys/uio.h>
#endif
#if defined(_WIN32)
#include <direct.h> /* _mkdir */
#else
#include <sys/stat.h>
#endif

static int pread_some(int fd, void *buf, size_t n, uint64_t off);
static int ensure_directory(const char *path);

#if defined(_WIN32)
static int make_dir_single(const char *path){
    if (!path || !*path) return 0;
    if (_mkdir(path)==0) return 0;
    if (errno==EEXIST) return 0;
    return -1;
}
#else
static int make_dir_single(const char *path){
    if (!path || !*path) return 0;
    if (mkdir(path, 0755)==0) return 0;
    if (errno==EEXIST) return 0;
    return -1;
}
#endif

static int ensure_directory(const char *path){
    if (!path || !*path) return 0;
    char tmp[1024];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)){ errno = ENAMETOOLONG; return -1; }
    memcpy(tmp, path, len+1);

    size_t start = 0;
#if defined(_WIN32)
    if (len >= 2 && tmp[1]==':') start = 2; // skip drive letter
#endif
    for (size_t i=start;i<len;i++){
        if (tmp[i]=='/' || tmp[i]=='\\'){
            char saved = tmp[i];
            tmp[i] = '\0';
            size_t sub_len = strlen(tmp);
#if defined(_WIN32)
            if (!(sub_len==2 && tmp[1]==':'))
#endif
            {
                if (sub_len>0 && strcmp(tmp,".")!=0 && strcmp(tmp,"..")!=0){
                    if (make_dir_single(tmp)!=0 && errno!=EEXIST) return -1;
                }
            }
            tmp[i] = saved;
        }
    }
    if (len>0 && strcmp(tmp,".")!=0 && strcmp(tmp,"..")!=0){
#if defined(_WIN32)
        if (len==2 && tmp[1]==':') return 0;
#endif
        if (make_dir_single(tmp)!=0 && errno!=EEXIST) return -1;
    }
    return 0;
}

extern double tig_score_deflate(const unsigned char*, size_t);
extern double tig_score_lzw(const unsigned char*, size_t);
extern double tig_score_packbits(const unsigned char*, size_t);
int tig_reassemble_order(const uint64_t*, const uint64_t*, uint32_t,
                         const double*, const double*, uint32_t**, uint32_t*);

// contiguous check
static int is_contiguous(const tig_tiff_ifd *v){
    if (v->count==0) return 0;
    for (uint64_t i=1;i<v->count;i++){
        if (v->offsets[i-1] + v->sizes[i-1] != v->offsets[i]) return 0;
    }
    return 1;
}

tig_extract_result tig_extract(int fd, const tig_tiff_header *h,
                               const tig_tiff_ifd *v, const char *out_dir, const char *stem)
{
    tig_extract_result R={0};
    (void)h;
    char base[1024];
    snprintf(base,sizeof(base), "%s/%s", out_dir, stem);

    // 1) contiguous → full
    if (is_contiguous(v)){
        uint64_t start=v->offsets[0];
        uint64_t end=v->offsets[v->count-1] + v->sizes[v->count-1];
        uint64_t len=end-start;
        unsigned char *buf = (unsigned char*)malloc((size_t)len);
        if (buf && pread_some(fd, buf, (size_t)len, start)==0){
            if (v->compression==1){
                char outp[1024]; snprintf(outp,sizeof(outp), "%s_full_uncomp.tif", base);
                if (tig_write_simple_tiff_uncompressed(outp, buf, (size_t)len,
                    v->image_width, v->image_length, v->bits_per_sample,
                    v->samples_per_pixel, v->photometric)==0){
                    R.full_ok=1; R.bytes_full=len;
                }
            } else {
                char outp[1024]; snprintf(outp,sizeof(outp), "%s_full.bin", base);
                FILE *f=fopen(outp,"wb"); if(f){ fwrite(buf,1,(size_t)len,f); fclose(f); R.bytes_full=len; }
            }
        }
        free(buf);
    }

    // 2) parçaları dump + akış skoru
    double *s_stream = (double*)calloc(v->count,sizeof(double));
    double *s_tex    = (double*)calloc(v->count,sizeof(double));
    if (!s_stream || !s_tex){
        if (!s_stream){
            LOGE("stream score allocation failed for %llu entries", (unsigned long long)v->count);
        }
        if (!s_tex){
            LOGE("texture score allocation failed for %llu entries", (unsigned long long)v->count);
        }
        free(s_stream);
        free(s_tex);
        return R;
    }
    for (uint64_t i=0;i<v->count;i++){
        char part[1024]; snprintf(part,sizeof(part), "%s_part_%04llu.bin", base, (unsigned long long)i);
        size_t n = (size_t)v->sizes[i];
        if (n==0 || n>(1u<<30)) continue;
        unsigned char *b = (unsigned char*)malloc(n);
        if (!b) continue;
        if (pread_some(fd, b, n, v->offsets[i])==0){
            FILE *pf=fopen(part,"wb"); if(pf){ fwrite(b,1,n,pf); fclose(pf); R.parts_written++; }
            if (v->compression==8) s_stream[i]=tig_score_deflate(b,n);
            else if (v->compression==5) s_stream[i]=tig_score_lzw(b,n);
            else if (v->compression==32773) s_stream[i]=tig_score_packbits(b,n);
            else s_stream[i]=0.5;
            s_tex[i]=0.0; // V3: texture continuity (only for uncompressed path)
        }
        free(b);
    }

    // 3) fragmente ise sıralama adayı
    if (!is_contiguous(v) && v->count>1){
        uint32_t *order=0, m=0;
        if (tig_reassemble_order(v->offsets, v->sizes, (uint32_t)v->count, s_stream, s_tex, &order, &m)==0 && order){
            char cand[1024]; snprintf(cand,sizeof(cand), "%s_candidate.bin", base);
            FILE *cf=fopen(cand,"wb");
            if (cf){
                for (uint32_t k=0;k<m;k++){
                    uint32_t i = order[k];
                    size_t n = (size_t)v->sizes[i]; if (n==0||n>(1u<<30)) continue;
                    unsigned char *b = (unsigned char*)malloc(n);
                    if (!b) break;
                    if (pread_some(fd,b,n,v->offsets[i])==0){ fwrite(b,1,n,cf); }
                    free(b);
                }
                fclose(cf);
            }
            free(order);
        }
    }

    free(s_stream); free(s_tex);
    return R;
}

// high-level scan + recover (linear scan; FS-free-space şimdilik opsiyonel)
int tig_engine_scan_recover(const char *img_path, const char *out_dir,
                            int prefer_free_space, const char *fs_list_csv,
                            uint64_t max_hits, int vlevel)
{
    (void)prefer_free_space; (void)fs_list_csv; (void)vlevel;
    if (ensure_directory(out_dir)!=0){
        LOGE("çıktı dizini oluşturulamadı: %s (%s)", out_dir, strerror(errno));
        return 1;
    }

    int fd = tig_open_ro(img_path);
    if (fd<0){ LOGE("imaj açılamadı: %s", img_path); return 1; }

    uint64_t next=0, hits=0;
    const uint64_t progress_interval = 1; // report every hit (can be increased)
    while (hits<max_hits){
        tig_tiff_header hdr;
        int rc = tig_find_next_tiff(fd, next, &hdr, &next);
        if (rc!=0){ LOGI("tarama bitti (%llu hit)", (unsigned long long)hits); break; }

        if ((hits % progress_interval) == 0){
            LOGI("scan progress: found %llu hits, next offset %llu", (unsigned long long)hits, (unsigned long long)next);
        }

        char stem[128]; snprintf(stem,sizeof(stem), "hit_%04llu", (unsigned long long)hits);
        tig_tiff_ifd ifd;
        if (tig_parse_ifd(fd, &hdr, &ifd)==0){
            tig_extract_result r = tig_extract(fd, &hdr, &ifd, out_dir, stem);
            // If we got a contiguous uncompressed TIFF full_ok, try to ensure .tif extension
            if (r.full_ok){
                // the extractor already wrote a file named <stem>_full_uncomp.tif when possible
                LOGI("recovered full TIFF for %s", stem);
            }
            tig_write_report(out_dir, stem, &hdr, &ifd, &r);
            tig_free_ifd(&ifd);
        }
        hits++;
    }
    tig_close(fd);
    return 0;
}

// dig range: start:end (byte offsets). Basit: aralığın başından başla, imza buldukça işle.
int tig_engine_dig_range(const char *img_path, tig_range range,
                         const char *out_dir, const char *heur_csv, int vlevel)
{
    (void)heur_csv; (void)vlevel;
    if (ensure_directory(out_dir)!=0){
        LOGE("çıktı dizini oluşturulamadı: %s (%s)", out_dir, strerror(errno));
        return 1;
    }
    int fd = tig_open_ro(img_path);
    if (fd<0){ LOGE("imaj açılamadı: %s", img_path); return 1; }

    uint64_t next=range.start_lba;
    uint64_t idx=0;
    while (next < range.end_lba){
        tig_tiff_header hdr;
        int rc = tig_find_next_tiff(fd, next, &hdr, &next);
        if (rc!=0 || hdr.hdr_off>=range.end_lba) break;
        char stem[128]; snprintf(stem,sizeof(stem), "range_hit_%04llu", (unsigned long long)idx++);
        tig_tiff_ifd ifd;
        if (tig_parse_ifd(fd, &hdr, &ifd)==0){
            tig_extract_result r = tig_extract(fd, &hdr, &ifd, out_dir, stem);
            tig_write_report(out_dir, stem, &hdr, &ifd, &r);
            tig_free_ifd(&ifd);
        }
    }
    tig_close(fd);
    return 0;
}

static int pread_some(int fd, void *buf, size_t n, uint64_t off){
#if defined(_WIN32)
    if (_lseeki64(fd, off, SEEK_SET)<0) return -1;
    size_t got=0; while (got<n){ int r=_read(fd,(char*)buf+got,(unsigned)(n-got)); if(r<=0)return -2; got+=(size_t)r; }
    return 0;
#else
    size_t got=0; while (got<n){ ssize_t r=pread(fd,(char*)buf+got,n-got,(off_t)off+got); if(r<=0)return -2; got+=(size_t)r; }
    return 0;
#endif
}
