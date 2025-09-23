#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "tigre.h"

// Stub implementations for dependencies that are irrelevant for this regression test.
double tig_score_deflate(const unsigned char *buf, size_t n) { (void)buf; (void)n; return 0.0; }
double tig_score_lzw(const unsigned char *buf, size_t n) { (void)buf; (void)n; return 0.0; }
double tig_score_packbits(const unsigned char *buf, size_t n) { (void)buf; (void)n; return 0.0; }
int tig_reassemble_order(const uint64_t *offsets, const uint64_t *sizes, uint32_t count,
                         const double *s_stream, const double *s_tex, uint32_t **order_out, uint32_t *m_out)
{
    (void)offsets; (void)sizes; (void)count; (void)s_stream; (void)s_tex; (void)order_out; (void)m_out;
    return -1;
}
int tig_write_simple_tiff_uncompressed(const char *path,
                                       const unsigned char *pixel, uint64_t nbytes,
                                       uint64_t w, uint64_t h, uint16_t bps,
                                       uint16_t spp, uint16_t photo)
{
    (void)path; (void)pixel; (void)nbytes; (void)w; (void)h; (void)bps; (void)spp; (void)photo;
    return -1;
}
int tig_write_report(const char *dir, const char *stem,
                     const tig_tiff_header *h, const tig_tiff_ifd *v,
                     const tig_extract_result *r)
{
    (void)dir; (void)stem; (void)h; (void)v; (void)r;
    return 0;
}
int tig_open_ro(const char *path) { (void)path; return -1; }
void tig_close(int fd) { (void)fd; }
int tig_find_next_tiff(int fd, uint64_t start_off, tig_tiff_header *out_hdr, uint64_t *next_hint)
{
    (void)fd; (void)start_off; (void)out_hdr; (void)next_hint;
    return -1;
}
int tig_parse_ifd(int fd, const tig_tiff_header *hdr, tig_tiff_ifd *out_ifd)
{
    (void)fd; (void)hdr; (void)out_ifd;
    return -1;
}
void tig_free_ifd(tig_tiff_ifd *v) { (void)v; }

// Pull in the implementation under test.
#include "../src/core/engine.c"

int main(void)
{
    tig_tiff_header hdr = {0};
    tig_tiff_ifd ifd;
    memset(&ifd, 0, sizeof(ifd));

    static const uint64_t offsets[2] = {0, 1024};
    static const uint64_t sizes[2] = {512, 512};

    ifd.image_width = 1;
    ifd.image_length = 1;
    ifd.bits_per_sample = 8;
    ifd.samples_per_pixel = 1;
    ifd.compression = 1;
    ifd.photometric = 1;
    ifd.planar_config = 1;
    ifd.count = (uint64_t)1 << 62; // Force calloc() to fail due to overflow/ENOMEM.
    ifd.offsets = offsets;
    ifd.sizes = sizes;

    errno = 0;
    tig_extract_result r = tig_extract(-1, &hdr, &ifd, ".", "oversized_ifd");

    assert(r.full_ok == 0);
    assert(r.parts_written == 0);
    assert(errno == ENOMEM);

    return 0;
}
