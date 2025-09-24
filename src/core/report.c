#include "tigre.h"
#include <stdio.h>

int tig_write_report(const char *dir, const char *stem,
                     const tig_tiff_header *h, const tig_tiff_ifd *v,
                     const tig_extract_result *r,
                     const tig_fragment_pool *pool,
                     const tig_validation_summary *summary)
{
    char p[1024]; snprintf(p,sizeof(p), "%s/%s_report.json", dir, stem);
    FILE *f=fopen(p,"w"); if(!f) return -1;
    fprintf(f,"{\n");
    fprintf(f,"  \"is_big_tiff\": %d,\n", h->is_big_tiff);
    fprintf(f,"  \"is_little_endian\": %d,\n", h->is_le);
    fprintf(f,"  \"hdr_offset\": %llu,\n", (unsigned long long)h->hdr_off);
    fprintf(f,"  \"first_ifd\": %llu,\n", (unsigned long long)h->first_ifd);
    fprintf(f,"  \"image_width\": %llu,\n", (unsigned long long)v->image_width);
    fprintf(f,"  \"image_length\": %llu,\n", (unsigned long long)v->image_length);
    fprintf(f,"  \"bits_per_sample\": %u,\n", v->bits_per_sample);
    fprintf(f,"  \"samples_per_pixel\": %u,\n", v->samples_per_pixel);
    fprintf(f,"  \"compression\": %u,\n", v->compression);
    fprintf(f,"  \"photometric\": %u,\n", v->photometric);
    fprintf(f,"  \"segments\": %llu,\n", (unsigned long long)v->count);
    fprintf(f,"  \"full_ok\": %d,\n", r->full_ok);
    fprintf(f,"  \"bytes_written_full\": %llu,\n", (unsigned long long)r->bytes_full);
    fprintf(f,"  \"parts_written\": %llu,\n", (unsigned long long)r->parts_written);
    fprintf(f,"  \"missing_parts\": %u,\n", r->missing_parts);
    fprintf(f,"  \"score_stream\": %.4f,\n", r->score_stream);
    fprintf(f,"  \"score_texture\": %.4f,\n", r->score_texture);
    fprintf(f,"  \"confidence\": %.4f,\n", r->confidence);
    if (summary){
        fprintf(f,"  \"coverage_ratio\": %.4f,\n", summary->coverage_ratio);
        fprintf(f,"  \"entropy_avg\": %.4f,\n", summary->entropy_avg);
        fprintf(f,"  \"stream_avg\": %.4f,\n", summary->stream_avg);
        fprintf(f,"  \"texture_avg\": %.4f,\n", summary->texture_avg);
        fprintf(f,"  \"segments_indexed\": %u,\n", summary->segments_indexed);
        fprintf(f,"  \"coverage_ok\": %d,\n", summary->coverage_ok);
    }
    fprintf(f,"  \"fragments\": [\n");
    if (pool && pool->items){
        for (size_t i=0;i<pool->count;i++){
            const tig_fragment_entry *ent = &pool->items[i];
            fprintf(f,"    {\"offset\": %llu, \"length\": %llu, \"compression\": %u, \"entropy\": %.4f, \"header_score\": %.4f, \"stream_score\": %.4f, \"texture_score\": %.4f}%s\n",
                    (unsigned long long)ent->offset,
                    (unsigned long long)ent->length,
                    ent->compression,
                    ent->entropy,
                    ent->header_score,
                    ent->stream_score,
                    ent->texture_score,
                    (i+1<pool->count)?",":"");
        }
    }
    fprintf(f,"  ]\n");
    fprintf(f,"}\n");
    fclose(f);
    return 0;
}
