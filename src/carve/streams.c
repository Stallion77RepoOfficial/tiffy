#include "tigre.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_ZLIB
#  include <zlib.h>
#endif

typedef struct { double score; } stream_score;

static double clamp01(double x){ if(x<0) return 0; if(x>1) return 1; return x; }

double tig_score_deflate(const unsigned char*buf, size_t n){
#ifndef USE_ZLIB
    (void)buf; (void)n; return 0.0; // zlib yoksa skorlayamıyoruz
#else
    // küçük bir pencere ile inflate test
    z_stream strm; memset(&strm,0,sizeof(strm));
    if (inflateInit(&strm)!=Z_OK) return 0.0;
    unsigned char out[1<<14];
    strm.next_in = (unsigned char*)buf; strm.avail_in = (unsigned)n;
    strm.next_out = out; strm.avail_out = (uInt)sizeof(out);
    int r = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);
    if (r==Z_STREAM_END || r==Z_BUF_ERROR || r==Z_OK){
        double ratio = (double)(sizeof(out)-strm.avail_out) / (double)sizeof(out);
        return clamp01(0.5 + 0.5*ratio); // biraz veri üretebildiysek +puan
    }
    return 0.0;
#endif
}

// Basit LZW ve PackBits kontrolleri: aşırı tutucu değil; yapısal anamolileri eleyelim
double tig_score_packbits(const unsigned char*buf, size_t n){
    // PackBits: (control byte) [-127..127], -n => tekrarlı, 0..127 => (n+1) literal
    size_t i=0, good=0, steps=0;
    while (i<n && steps<1024){
        int8_t ctrl = (int8_t)buf[i++];
        steps++;
        if (ctrl>=0){ size_t lit = (size_t)ctrl + 1; if (i+lit>n) break; i+=lit; good++; }
        else if (ctrl>=-127){ if (i>=n) break; i+=1; good++; }
        else { /* -128 no-op */ good++; }
    }
    double d = (double)good/(double)steps;
    return clamp01(d);
}

// LZW (TIFF) için hızlı imha testi: sözlük girişlerinin taşması vb. (tam decode değil)
double tig_score_lzw(const unsigned char*buf, size_t n){
    if (n<16) return 0.0;
    // kaba entropi: çok düşük entropi -> şüpheli; çok yüksek -> sıkıştırmalı olabilir
    // hızlı kol: bit-pattern dağılımı
    size_t ones=0;
    for (size_t i=0;i<n && i<4096;i++){ unsigned char b=buf[i]; ones += __builtin_popcount((unsigned)b); }
    double ratio = (double)ones / (8.0 * (double)((n<4096)?n:4096));
    // 0.35..0.75 arası makul
    if (ratio<0.25 || ratio>0.85) return 0.1;
    return 0.6; // kaba skor
}
