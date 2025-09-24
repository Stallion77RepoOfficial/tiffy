#include "tigre.h"
#include <string.h>

static double clamp01(double x){
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

int tig_validation_summarize(const tig_tiff_ifd *ifd,
                              const tig_fragment_pool *pool,
                              const double *stream_scores,
                              const double *texture_scores,
                              uint64_t count,
                              uint32_t missing_parts,
                              tig_validation_summary *out_summary)
{
    if (!out_summary) return -1;
    memset(out_summary, 0, sizeof(*out_summary));

    double stream_sum = 0.0, texture_sum = 0.0;
    uint64_t score_items = 0;
    if (stream_scores && texture_scores){
        for (uint64_t i=0;i<count;i++){
            stream_sum  += stream_scores[i];
            texture_sum += texture_scores[i];
            score_items++;
        }
    }
    if (score_items>0){
        out_summary->stream_avg  = clamp01(stream_sum  / (double)score_items);
        out_summary->texture_avg = clamp01(texture_sum / (double)score_items);
    }

    double entropy_sum = 0.0;
    uint64_t captured_len = 0;
    uint32_t indexed = 0;
    if (pool && pool->items){
        indexed = (uint32_t)pool->count;
        for (size_t i=0;i<pool->count;i++){
            const tig_fragment_entry *ent = &pool->items[i];
            entropy_sum += ent->entropy;
            if (ent->length < ((uint64_t)1<<62)){
                captured_len += (uint64_t)ent->length;
            }
        }
    }
    out_summary->segments_indexed = indexed;
    if (indexed>0){
        out_summary->entropy_avg = clamp01(entropy_sum / (double)indexed);
    }

    double expected_len = 0.0;
    if (ifd && ifd->sizes){
        for (uint64_t i=0;i<count && i<ifd->count;i++){
            expected_len += (double)ifd->sizes[i];
        }
    }
    if (expected_len > 0.0){
        double cov = (double)captured_len / expected_len;
        if (cov > 1.0) cov = 1.0;
        if (cov < 0.0) cov = 0.0;
        out_summary->coverage_ratio = cov;
    } else {
        out_summary->coverage_ratio = 0.0;
    }

    out_summary->missing_parts = missing_parts;
    out_summary->coverage_ok = (out_summary->coverage_ratio >= 0.95 && missing_parts == 0);

    double coverage_term = out_summary->coverage_ratio;
    double stream_term   = out_summary->stream_avg;
    double texture_term  = out_summary->texture_avg;
    double entropy_term  = out_summary->entropy_avg;

    double confidence = 0.15 + 0.40*stream_term + 0.25*coverage_term + 0.10*texture_term + 0.10*entropy_term;
    if (indexed == 0) confidence *= 0.6;
    if (missing_parts > 0){
        double penalty = 1.0 / (1.0 + (double)missing_parts);
        confidence *= penalty;
    }
    out_summary->confidence = clamp01(confidence);
    return 0;
}
