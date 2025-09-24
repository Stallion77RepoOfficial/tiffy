#ifndef TIGRE_H
#define TIGRE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Project metadata */
#ifndef PROJECT_NAME
#define PROJECT_NAME "tiffy"
#endif
#ifndef PROJECT_VERSION
#define PROJECT_VERSION "0.1.0"
#endif
#ifndef PROJECT_AUTHOR
#define PROJECT_AUTHOR "Stallion77"
#endif

typedef struct {
    int is_big_tiff;      // 0 classic, 1 bigtiff
    int is_le;            // little-endian?
    uint64_t hdr_off;     // absolute header offset in image
    uint64_t first_ifd;   // absolute first IFD offset
} tig_tiff_header;

typedef struct {
    uint64_t image_width, image_length;
    uint16_t bits_per_sample, samples_per_pixel;
    uint16_t compression, photometric, planar_config, predictor;
    uint32_t use_tiles;          // 0 strips, 1 tiles
    uint64_t count;              // strip/tile count
    const uint64_t *offsets;     // abs offsets
    const uint64_t *sizes;       // byte counts
    uint64_t rows_per_strip;     // 278
    uint64_t tile_width;         // 322
    uint64_t tile_length;        // 323
    void *__opaque;              // ownership flag
} tig_tiff_ifd;

typedef struct {
    int      full_ok;
    uint64_t bytes_full;
    uint64_t parts_written;
    uint32_t missing_parts;
    double   score_stream;     // 0..1 (Deflate/LZW checks)
    double   score_texture;    // 0..1 (V3 texture continuity)
    double   confidence;       // 0..1 birleşik güven puanı
} tig_extract_result;

typedef struct {
    uint64_t start_lba;
    uint64_t end_lba;
} tig_range;

typedef struct {
    uint64_t offset;
    uint64_t length;
    uint16_t compression;
    uint64_t hash64;
    double   entropy;
    double   header_score;
    double   stream_score;
    double   texture_score;
    unsigned char *capture;
    size_t   capture_len;
} tig_fragment_entry;

typedef struct {
    tig_fragment_entry *items;
    size_t count;
    size_t capacity;
    size_t capture_limit;
} tig_fragment_pool;

typedef struct {
    uint64_t offset;
    uint64_t length;
    uint16_t compression;
    uint64_t hash64;
    double   entropy;
    double   header_score;
    double   stream_score;
    double   texture_score;
    const unsigned char *data;
    size_t   data_len;
    double   match_score;
} tig_fragment_match;

typedef struct {
    double   stream_avg;
    double   texture_avg;
    double   entropy_avg;
    double   coverage_ratio;
    uint32_t segments_indexed;
    uint32_t missing_parts;
    double   confidence;
    int      coverage_ok;
} tig_validation_summary;

// io
int  tig_open_ro(const char *path); // returns fd >=0
void tig_close(int fd);

// clone (ALWAYS read-only source → image)
int tig_clone_to_img(const char *src_dev, const char *dst_img,
                     const char *map_json, uint64_t block_size,
                     int retry, int skip_on_error);

// scan (signature-based)
int tig_find_next_tiff(int fd, uint64_t start_off, tig_tiff_header *out_hdr, uint64_t *next_hint);

// parse IFD (TIFF + BigTIFF)
int  tig_parse_ifd(int fd, const tig_tiff_header *hdr, tig_tiff_ifd *out_ifd);
void tig_free_ifd(tig_tiff_ifd *v);

// extraction (dump + optional rebuild)
tig_extract_result tig_extract(int fd, const tig_tiff_header *hdr,
                               const tig_tiff_ifd *ifd,
                               const char *out_dir, const char *stem,
                               tig_fragment_pool *pool_out,
                               tig_validation_summary *summary_out);

// fragment yönetimi
int  tig_fragment_pool_init(tig_fragment_pool *pool, size_t capacity_hint, size_t capture_limit);
void tig_fragment_pool_reset(tig_fragment_pool *pool);
void tig_fragment_pool_free(tig_fragment_pool *pool);
int  tig_fragment_pool_ingest(tig_fragment_pool *pool, uint64_t offset,
                              const unsigned char *buf, size_t len,
                              uint16_t compression, double stream_score,
                              double texture_score);
int  tig_fragment_pool_find_candidate(const tig_fragment_pool *pool,
                                      uint64_t desired_len, uint16_t compression,
                                      double tolerance, tig_fragment_match *out);

// doğrulama
int  tig_validation_summarize(const tig_tiff_ifd *ifd,
                              const tig_fragment_pool *pool,
                              const double *stream_scores,
                              const double *texture_scores,
                              uint64_t count,
                              uint32_t missing_parts,
                              tig_validation_summary *out_summary);

// write minimal TIFF (uncompressed single-strip)
int tig_write_simple_tiff_uncompressed(const char *path,
                                       const unsigned char *pixel, uint64_t nbytes,
                                       uint64_t w, uint64_t h, uint16_t bps,
                                       uint16_t spp, uint16_t photo);

// high-level engine
int tig_engine_scan_recover(const char *img_path, const char *out_dir,
                            int prefer_free_space, const char *fs_list_csv,
                            uint64_t max_hits, int vlevel);

int tig_engine_dig_range(const char *img_path, tig_range range,
                         const char *out_dir, const char *heur_csv, int vlevel);

// reports
int tig_write_report(const char *dir, const char *stem,
                     const tig_tiff_header *h, const tig_tiff_ifd *v,
                     const tig_extract_result *r,
                     const tig_fragment_pool *pool,
                     const tig_validation_summary *summary);

#ifdef __cplusplus
}
#endif

#endif
