#include <assert.h>
#include <string.h>

#include "tigre.h"

int main(void)
{
    tig_fragment_pool pool;
    assert(tig_fragment_pool_init(&pool, 2, 64) == 0);

    static const unsigned char deflate_data[] = {
        0x78, 0x9C, 0x63, 0x60, 0x18, 0x05, 0xA3, 0x60
    };
    static const unsigned char jpeg_data[] = {
        0xFF, 0xD8, 0xFF, 0xE0, 0x11, 0x22, 0x33, 0x44
    };

    assert(tig_fragment_pool_ingest(&pool, 1024, deflate_data, sizeof(deflate_data), 8, 0.8, 0.1) == 0);
    assert(tig_fragment_pool_ingest(&pool, 4096, jpeg_data, sizeof(jpeg_data), 7, 0.7, 0.2) == 0);

    tig_fragment_match match;
    assert(tig_fragment_pool_find_candidate(&pool, sizeof(deflate_data), 8, 0.3, &match) == 0);
    assert(match.offset == 1024);
    assert(match.match_score >= 0.3);
    assert(match.data != NULL);

    tig_tiff_ifd ifd;
    memset(&ifd, 0, sizeof(ifd));
    static const uint64_t sizes[2] = {sizeof(deflate_data), sizeof(jpeg_data)};
    ifd.count = 2;
    ifd.sizes = sizes;

    double stream_scores[2] = {0.8, 0.7};
    double texture_scores[2] = {0.1, 0.2};
    tig_validation_summary summary;
    assert(tig_validation_summarize(&ifd, &pool, stream_scores, texture_scores, 2, 0, &summary) == 0);
    assert(summary.segments_indexed == 2);
    assert(summary.confidence > 0.4);

    tig_fragment_pool_free(&pool);
    return 0;
}
