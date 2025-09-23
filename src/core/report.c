#include "tigre.h"
#include <stdio.h>

int tig_write_report(const char *dir, const char *stem,
                     const tig_tiff_header *h, const tig_tiff_ifd *v,
                     const tig_extract_result *r)
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
    fprintf(f,"  \"parts_written\": %llu\n", (unsigned long long)r->parts_written);
    fprintf(f,"}\n");
    fclose(f);
    return 0;
}
