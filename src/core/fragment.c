#include "tigre.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint64_t fnv1a64(const unsigned char *buf, size_t len){
    const uint64_t FNV_OFFSET = 0xcbf29ce484222325ULL;
    const uint64_t FNV_PRIME  = 0x100000001b3ULL;
    uint64_t h = FNV_OFFSET;
    for (size_t i=0;i<len;i++){
        h ^= (uint64_t)buf[i];
        h *= FNV_PRIME;
    }
    return h;
}

static double clamp01(double x){
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

static double entropy_estimate(const unsigned char *buf, size_t len){
    if (!buf || len == 0) return 0.0;
    double hist[256];
    for (int i=0;i<256;i++) hist[i]=0.0;
    for (size_t i=0;i<len;i++) hist[buf[i]] += 1.0;
    double inv = 1.0 / (double)len;
    double ent = 0.0;
    for (int i=0;i<256;i++){
        if (hist[i] <= 0.0) continue;
        double p = hist[i] * inv;
        ent -= p * (log(p) / log(2.0));
    }
    return clamp01(ent / 8.0);
}

static double header_score(uint16_t compression, const unsigned char *buf, size_t len){
    if (!buf || len < 4) return 0.0;
    switch (compression){
        case 1: // Uncompressed
            return 0.5; // nötr
        case 5: // LZW
            return 0.5 + 0.3 * entropy_estimate(buf, len);
        case 7: // JPEG baseline
        case 99:
            if (len >= 2 && buf[0]==0xFF && buf[1]==0xD8) return 0.9;
            return 0.2;
        case 8: // Deflate
            if ((buf[0] == 0x78 && (buf[1] == 0x9C || buf[1] == 0xDA)) ||
                (buf[0] == 0x08 && buf[1] == 0x1D)){
                return 0.8;
            }
            return 0.4;
        case 32773: // PackBits
            return 0.5;
        default:
            return 0.3;
    }
}

static int ensure_capacity(tig_fragment_pool *pool, size_t need){
    if (pool->capacity >= need) return 0;
    size_t new_cap = pool->capacity ? pool->capacity : 8;
    while (new_cap < need){
        new_cap *= 2;
        if (new_cap < pool->capacity) { // overflow guard
            new_cap = need;
            break;
        }
    }
    tig_fragment_entry *tmp = (tig_fragment_entry*)realloc(pool->items, new_cap * sizeof(tig_fragment_entry));
    if (!tmp) return -1;
    // yeni alanı temizle
    for (size_t i=pool->capacity;i<new_cap;i++){
        memset(&tmp[i], 0, sizeof(tig_fragment_entry));
    }
    pool->items = tmp;
    pool->capacity = new_cap;
    return 0;
}

int tig_fragment_pool_init(tig_fragment_pool *pool, size_t capacity_hint, size_t capture_limit){
    if (!pool) return -1;
    memset(pool, 0, sizeof(*pool));
    pool->capture_limit = capture_limit ? capture_limit : (size_t)(1<<20);
    size_t init_cap = capacity_hint ? capacity_hint : 8;
    pool->items = (tig_fragment_entry*)calloc(init_cap, sizeof(tig_fragment_entry));
    if (!pool->items){
        pool->capacity = 0;
        return -1;
    }
    pool->capacity = init_cap;
    return 0;
}

void tig_fragment_pool_reset(tig_fragment_pool *pool){
    if (!pool) return;
    for (size_t i=0;i<pool->count;i++){
        free(pool->items[i].capture);
        pool->items[i].capture = NULL;
        pool->items[i].capture_len = 0;
    }
    pool->count = 0;
}

void tig_fragment_pool_free(tig_fragment_pool *pool){
    if (!pool) return;
    tig_fragment_pool_reset(pool);
    free(pool->items);
    pool->items = NULL;
    pool->capacity = 0;
    pool->capture_limit = 0;
}

int tig_fragment_pool_ingest(tig_fragment_pool *pool, uint64_t offset,
                              const unsigned char *buf, size_t len,
                              uint16_t compression, double stream_score,
                              double texture_score)
{
    if (!pool || !buf || len==0) return -1;
    if (ensure_capacity(pool, pool->count + 1)!=0) return -2;
    tig_fragment_entry *ent = &pool->items[pool->count];
    memset(ent, 0, sizeof(*ent));
    ent->offset = offset;
    ent->length = len;
    ent->compression = compression;
    ent->hash64 = fnv1a64(buf, len);
    ent->entropy = entropy_estimate(buf, len);
    ent->header_score = header_score(compression, buf, len);
    ent->stream_score = stream_score;
    ent->texture_score = texture_score;
    size_t cap = len;
    if (pool->capture_limit && cap > pool->capture_limit) cap = pool->capture_limit;
    if (cap>0){
        ent->capture = (unsigned char*)malloc(cap);
        if (ent->capture){
            memcpy(ent->capture, buf, cap);
            ent->capture_len = cap;
        }
    }
    pool->count++;
    return 0;
}

int tig_fragment_pool_find_candidate(const tig_fragment_pool *pool,
                                      uint64_t desired_len, uint16_t compression,
                                      double tolerance, tig_fragment_match *out)
{
    if (!pool || !out) return -1;
    memset(out, 0, sizeof(*out));
    if (pool->count==0 || desired_len==0) return -2;
    double best=-1.0;
    const tig_fragment_entry *best_ent=NULL;
    for (size_t i=0;i<pool->count;i++){
        const tig_fragment_entry *ent = &pool->items[i];
        double len_min = (double)(ent->length < desired_len ? ent->length : desired_len);
        double len_max = (double)(ent->length > desired_len ? ent->length : desired_len);
        double len_score = len_max>0 ? (len_min / len_max) : 0.0;
        double comp_score = (ent->compression == compression) ? 1.0 : 0.25;
        double header = ent->header_score;
        double stream = ent->stream_score;
        double texture = ent->texture_score;
        double entropy = ent->entropy;
        double combined = 0.35*len_score + 0.20*comp_score + 0.15*header + 0.15*stream + 0.15*entropy;
        if (texture > 0.0) combined += 0.05*texture;
        if (combined > best){
            best = combined;
            best_ent = ent;
        }
    }
    if (!best_ent || best < tolerance) return -3;
    out->offset = best_ent->offset;
    out->length = best_ent->length;
    out->compression = best_ent->compression;
    out->hash64 = best_ent->hash64;
    out->entropy = best_ent->entropy;
    out->header_score = best_ent->header_score;
    out->stream_score = best_ent->stream_score;
    out->texture_score = best_ent->texture_score;
    out->data = best_ent->capture;
    out->data_len = best_ent->capture_len;
    out->match_score = clamp01(best);
    return 0;
}
