#include "tigre.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    uint64_t off, len;
    double s_stream;  // akış skoru
    double s_tex;     // doku skoru (varsa)
} seg_feat;

typedef struct {
    uint32_t *order;  // seçilen sıra
    uint32_t count;
    double total_score;
} reasm_plan;

static double w_near=0.25, w_len=0.15, w_stream=0.40, w_tex=0.20;

static double score_pair(const seg_feat*a, const seg_feat*b){
    // yakınlık: ofset bitişik veya yakınsa puan
    double near = (a->off + a->len == b->off) ? 1.0 : (fabs((double)((int64_t)( (a->off+a->len) - b->off ))) < (double)(1<<20) ? 0.6:0.0);
    // boyut benzerliği (heuristic)
    double lenr = (double)((a->len<b->len)?a->len:b->len)/(double)((a->len>b->len)?a->len:b->len);
    double stream = 0.5*(a->s_stream + b->s_stream);
    double tex = 0.5*(a->s_tex + b->s_tex);
    return w_near*near + w_len*lenr + w_stream*stream + w_tex*tex;
}

// basit en iyi-ekleme (greedy) + local swap
static reasm_plan build_plan(seg_feat *feats, uint32_t n){
    reasm_plan p={0};
    if (n==0) return p;
    p.order = (uint32_t*)malloc(sizeof(uint32_t)*n);
    // başlangıç: en yüksek stream skorlu parçayı başa
    double best=-1; uint32_t bi=0;
    for (uint32_t i=0;i<n;i++){ double s=feats[i].s_stream + 0.2*feats[i].s_tex; if (s>best){best=s;bi=i;} }
    p.order[0]=bi; p.count=1;
    int *used = (int*)calloc(n, sizeof(int)); used[bi]=1;
    while (p.count<n){
        double bsc=-1; uint32_t bj=0;
        for (uint32_t j=0;j<n;j++){
            if (used[j]) continue;
            double s = score_pair(&feats[p.order[p.count-1]], &feats[j]);
            if (s>bsc){ bsc=s; bj=j; }
        }
        p.order[p.count++]=bj; used[bj]=1; p.total_score += bsc;
    }
    free(used);
    // küçük local swap’larla iyileştirme (2-opt benzeri)
    for (int iter=0; iter<8; iter++){
        int improved=0;
        for (uint32_t i=1;i+1<p.count;i++){
            double cur = score_pair(&feats[p.order[i-1]], &feats[p.order[i]]) +
                         score_pair(&feats[p.order[i]], &feats[p.order[i+1]]);
            double alt = score_pair(&feats[p.order[i-1]], &feats[p.order[i+1]]) +
                         score_pair(&feats[p.order[i+1]], &feats[p.order[i]]);
            if (alt>cur){
                uint32_t tmp=p.order[i]; p.order[i]=p.order[i+1]; p.order[i+1]=tmp;
                p.total_score += (alt-cur); improved=1;
            }
        }
        if (!improved) break;
    }
    return p;
}

// dış API (özet): offsets/sizes’ı sırala (fragmente ise) ve sırayı döndür
int tig_reassemble_order(const uint64_t *offs, const uint64_t *lens, uint32_t n,
                         const double *s_stream, const double *s_tex,
                         uint32_t **out_order, uint32_t *out_n)
{
    if (n==0) return -1;
    seg_feat *f = (seg_feat*)malloc(sizeof(seg_feat)*n);
    for (uint32_t i=0;i<n;i++){
        f[i].off=offs[i]; f[i].len=lens[i];
        f[i].s_stream = s_stream? s_stream[i]: 0.5;
        f[i].s_tex    = s_tex? s_tex[i] : 0.0;
    }
    reasm_plan p = build_plan(f, n);
    *out_order = p.order; *out_n = p.count;
    free(f);
    return 0;
}
