#include "tigre.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_all(FILE *f, const void*buf, size_t n){ return fwrite(buf,1,n,f)==n?0:-1; }

int tig_write_simple_tiff_uncompressed(const char *path,
                                       const unsigned char *pixel, uint64_t nbytes,
                                       uint64_t w, uint64_t h, uint16_t bps,
                                       uint16_t spp, uint16_t photo)
{
    // Minimal baseline little-endian TIFF with single strip, no compression.
    FILE *fp = fopen(path,"wb"); if(!fp) return -1;
    // Header: "II *\0"
    unsigned char hdr[8] = {0x49,0x49,0x2A,0x00, 0,0,0,0};
    uint32_t ifd_off = 8 + (uint32_t)nbytes; // after img data
    memcpy(hdr+4, &ifd_off, 4);
    if (write_all(fp, hdr, sizeof(hdr))!=0){ fclose(fp); return -2; }

    // Image data
    if (write_all(fp, pixel, (size_t)nbytes)!=0){ fclose(fp); return -3; }

    // IFD entries (we will write 9 entries)
    uint16_t count = 9;
    if (write_all(fp, &count, 2)!=0){ fclose(fp); return -4; }

    // Helper lambdas
    #define WTAG(TAG,TYPE,COUNT,VALOFF) do{ \
        uint16_t _t=(TAG), _ty=(TYPE); uint32_t _c=(COUNT), _vo=(VALOFF); \
        write_all(fp,&_t,2); write_all(fp,&_ty,2); write_all(fp,&_c,4); write_all(fp,&_vo,4); }while(0)

    // We'll need a small data area after entries for some 4+ byte fields (like BitsPerSample if spp>1)
    long pos_entries_end = ftell(fp) + count*12 + 4; // +nextIFD
    // Precompute values to place inline vs later
    uint32_t zero=0;
    uint32_t img_w=(uint32_t)w, img_h=(uint32_t)h, rows_per_strip=(uint32_t)h;
    /* bps_arr, xres_off, yres_off are not needed in this minimal writer; keep only used fields */
    uint32_t strip_off = 8;
    uint32_t strip_len = (uint32_t)nbytes;
    uint32_t bps_off=0;

    // 256 ImageWidth (LONG,1)
    WTAG(256,4,1,img_w);
    // 257 ImageLength
    WTAG(257,4,1,img_h);
    // 258 BitsPerSample (SHORT, spp items)
    if (spp==1){
        uint32_t v = bps; WTAG(258,3,1,v);
    } else {
        bps_off = (uint32_t)pos_entries_end; WTAG(258,3,spp,bps_off);
    }
    // 259 Compression (SHORT,1) = 1 (None)
    { uint32_t v=1; WTAG(259,3,1,v); }
    // 262 PhotometricInterpretation (SHORT,1)
    { uint32_t v=photo; WTAG(262,3,1,v); }
    // 273 StripOffsets (LONG,1)
    WTAG(273,4,1,strip_off);
    // 277 SamplesPerPixel (SHORT,1)
    { uint32_t v=spp; WTAG(277,3,1,v); }
    // 278 RowsPerStrip (LONG,1)
    WTAG(278,4,1,rows_per_strip);
    // 279 StripByteCounts (LONG,1)
    WTAG(279,4,1,strip_len);

    // nextIFD=0
    write_all(fp, &zero, 4);

    // data area after entries
    if (bps_off){
        long cur = ftell(fp);
        fseek(fp, bps_off, SEEK_SET);
        for (int i=0;i<spp;i++){ uint16_t v=bps; write_all(fp,&v,2); }
        fseek(fp, cur, SEEK_SET);
    }

    fclose(fp);
    return 0;
}
