#include "tigre.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

static int pread_all(int fd, void *buf, size_t n, uint64_t off);

static inline uint16_t U16(const unsigned char*p,int le){return le?(p[0]|(p[1]<<8)):((p[0]<<8)|p[1]);}
static inline uint32_t U32(const unsigned char*p,int le){return le?(p[0]|(p[1]<<8)|(p[2]<<16)|(p[3]<<24))
                                                                :((p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]);}
static inline uint64_t U64(const unsigned char*p,int le){
    if (le) return (uint64_t)p[0]|((uint64_t)p[1]<<8)|((uint64_t)p[2]<<16)|((uint64_t)p[3]<<24)|
                    ((uint64_t)p[4]<<32)|((uint64_t)p[5]<<40)|((uint64_t)p[6]<<48)|((uint64_t)p[7]<<56);
    return ((uint64_t)p[0]<<56)|((uint64_t)p[1]<<48)|((uint64_t)p[2]<<40)|((uint64_t)p[3]<<32)|
           ((uint64_t)p[4]<<24)|((uint64_t)p[5]<<16)|((uint64_t)p[6]<<8)|p[7];
}

static int read_array64(int fd, int big, int le, int type, uint64_t count, uint64_t val_or_off,
                        uint64_t base, uint64_t **out)
{
    // normalize to 64-bit array
    size_t el = (type==3?2 : type==4?4 : 8);
    size_t total = (size_t)(count * el);
    unsigned char *tmp = malloc(total);
    if (!tmp) return -1;
    uint64_t abs = big ? val_or_off : (base + val_or_off);
    if (pread_all(fd, tmp, total, abs)!=0){ free(tmp); return -2; }
    uint64_t *a = (uint64_t*)malloc(sizeof(uint64_t)*count);
    if (!a){ free(tmp); return -3; }
    for (uint64_t i=0;i<count;i++){
        const unsigned char *p = tmp + i*el;
        if (el==2) a[i] = (uint64_t)U16(p, le);
        else if (el==4) a[i] = (uint64_t)U32(p, le);
        else a[i] = U64(p, le);
    }
    free(tmp);
    *out = a; return 0;
}

int tig_parse_ifd(int fd, const tig_tiff_header *hdr, tig_tiff_ifd *out){
    memset(out, 0, sizeof(*out));
    uint64_t base = hdr->hdr_off;
    uint64_t ifd = hdr->first_ifd;

    unsigned char nbuf[8];
    if (pread_all(fd, nbuf, hdr->is_big_tiff?8:2, ifd)!=0) return -1;
    uint64_t n = hdr->is_big_tiff ? U64(nbuf, hdr->is_le) : (uint64_t)U16(nbuf, hdr->is_le);

    uint64_t esz = hdr->is_big_tiff?20:12;
    uint64_t eoff = ifd + (hdr->is_big_tiff?8:2);
    unsigned char *ents = malloc((size_t)(n*esz));
    if (!ents) return -2;
    if (pread_all(fd, ents, (size_t)(n*esz), eoff)!=0){ free(ents); return -3; }

    uint64_t *strip_offs=0,*strip_sz=0, sc=0;
    uint64_t *tile_offs=0,*tile_sz=0, tc=0;
    uint64_t rows_per_strip=0, tile_w=0, tile_h=0;
    uint64_t iw=0, ih=0; uint16_t bps=0,spp=0,comp=1,photo=0,planar=1,predict=1;

    for (uint64_t i=0;i<n;i++){
        unsigned char *e = ents + i*esz;
        uint16_t tag = U16(e+0, hdr->is_le);
        uint16_t typ = U16(e+2, hdr->is_le);
        uint64_t cnt = hdr->is_big_tiff ? U64(e+4, hdr->is_le) : (uint64_t)U32(e+4, hdr->is_le);
        uint64_t val = hdr->is_big_tiff ? U64(e+12, hdr->is_le) : (uint64_t)U32(e+8, hdr->is_le);

        switch(tag){
            case 256: // ImageWidth
                if (cnt==1){
                    if (!hdr->is_big_tiff && (typ==4 || typ==3)){
                        iw = (typ==4)? (uint64_t)(uint32_t)val : (uint64_t)(uint16_t)(val & 0xFFFF);
                    } else if (hdr->is_big_tiff && (typ==16 || typ==4)){
                        iw = (typ==16)? val : (uint64_t)(uint32_t)val;
                    } else {
                        uint64_t *tmp=0; if (read_array64(fd, hdr->is_big_tiff, hdr->is_le, typ, 1, val, base, &tmp)==0){ iw=tmp[0]; free(tmp); }
                    }
                } break;
            case 257: // ImageLength
                if (cnt==1){
                    if (!hdr->is_big_tiff && (typ==4 || typ==3)){
                        ih = (typ==4)? (uint64_t)(uint32_t)val : (uint64_t)(uint16_t)(val & 0xFFFF);
                    } else if (hdr->is_big_tiff && (typ==16 || typ==4)){
                        ih = (typ==16)? val : (uint64_t)(uint32_t)val;
                    } else {
                        uint64_t *tmp=0; if (read_array64(fd, hdr->is_big_tiff, hdr->is_le, typ, 1, val, base, &tmp)==0){ ih=tmp[0]; free(tmp); }
                    }
                } break;
            case 258: // BitsPerSample
                if (cnt>=1){
                    /* If the field type is SHORT (3), the value/offset may contain the first channel inline. */
                    if (typ==3) {
                        bps = (uint16_t)((val) & 0xFFFF); // first channel
                    } else {
                        uint64_t *tmp=0; if (read_array64(fd, hdr->is_big_tiff, hdr->is_le, typ, 1, val, base, &tmp)==0){ bps=(uint16_t)tmp[0]; free(tmp); }
                    }
                } break;
            case 277: // SamplesPerPixel
                if (cnt==1){
                    if (typ==3) spp = (uint16_t)(val & 0xFFFF);
                    else { uint64_t *tmp=0; if (read_array64(fd, hdr->is_big_tiff, hdr->is_le, typ, 1, val, base, &tmp)==0){ spp=(uint16_t)tmp[0]; free(tmp); } }
                } break;
            case 259: // Compression
                if (cnt==1){
                    if (typ==3) comp = (uint16_t)(val & 0xFFFF);
                    else { uint64_t *tmp=0; if (read_array64(fd, hdr->is_big_tiff, hdr->is_le, typ, 1, val, base, &tmp)==0){ comp=(uint16_t)tmp[0]; free(tmp); } }
                } break;
            case 262: // Photometric
                if (cnt==1){
                    if (typ==3) photo = (uint16_t)(val & 0xFFFF);
                    else { uint64_t *tmp=0; if (read_array64(fd, hdr->is_big_tiff, hdr->is_le, typ, 1, val, base, &tmp)==0){ photo=(uint16_t)tmp[0]; free(tmp); } }
                } break;
            case 284: // PlanarConfiguration
                if (cnt==1){
                    if (typ==3) planar = (uint16_t)(val & 0xFFFF);
                    else { uint64_t *tmp=0; if (read_array64(fd, hdr->is_big_tiff, hdr->is_le, typ, 1, val, base, &tmp)==0){ planar=(uint16_t)tmp[0]; free(tmp); } }
                } break;
            case 317: // Predictor (LZW/Deflate için)
                if (cnt==1){
                    if (typ==3) predict = (uint16_t)(val & 0xFFFF);
                    else { uint64_t *tmp=0; if (read_array64(fd, hdr->is_big_tiff, hdr->is_le, typ, 1, val, base, &tmp)==0){ predict=(uint16_t)tmp[0]; free(tmp); } }
                } break;
            case 278: { // RowsPerStrip
                uint64_t *tmp=0; if (read_array64(fd, hdr->is_big_tiff, hdr->is_le, typ, 1, val, base, &tmp)==0){ rows_per_strip=tmp[0]; free(tmp); }
            } break;
            case 322: { // TileWidth
                uint64_t *tmp=0; if (read_array64(fd, hdr->is_big_tiff, hdr->is_le, typ, 1, val, base, &tmp)==0){ tile_w=tmp[0]; free(tmp); }
            } break;
            case 323: { // TileLength
                uint64_t *tmp=0; if (read_array64(fd, hdr->is_big_tiff, hdr->is_le, typ, 1, val, base, &tmp)==0){ tile_h=tmp[0]; free(tmp); }
            } break;
            case 273: { // StripOffsets
                sc = cnt; read_array64(fd, hdr->is_big_tiff, hdr->is_le, typ, cnt, val, base, &strip_offs);
            } break;
            case 279: { // StripByteCounts
                read_array64(fd, hdr->is_big_tiff, hdr->is_le, typ, cnt, val, base, &strip_sz);
            } break;
            case 324: { // TileOffsets
                tc = cnt; read_array64(fd, hdr->is_big_tiff, hdr->is_le, typ, cnt, val, base, &tile_offs);
            } break;
            case 325: { // TileByteCounts
                read_array64(fd, hdr->is_big_tiff, hdr->is_le, typ, cnt, val, base, &tile_sz);
            } break;
            default: break;
        }
    }
    free(ents);

    out->image_width = iw; out->image_length = ih;
    out->bits_per_sample = bps; out->samples_per_pixel = spp;
    out->compression = comp; out->photometric = photo;
    out->planar_config = planar; out->predictor = predict;
    out->rows_per_strip = rows_per_strip;
    out->tile_width = tile_w; out->tile_length = tile_h;

    if (tile_offs && tile_sz && tc>0){
        out->use_tiles = 1; out->count = tc;
        out->offsets = tile_offs; out->sizes = tile_sz; out->__opaque=(void*)1;
    } else if (strip_offs && strip_sz && sc>0){
        out->use_tiles = 0; out->count = sc;
        out->offsets = strip_offs; out->sizes = strip_sz; out->__opaque=(void*)1;
    } else {
        if (strip_offs) free(strip_offs);
        if (strip_sz) free(strip_sz);
        if (tile_offs) free(tile_offs);
        if (tile_sz) free(tile_sz);
        return -4;
    }
    return 0;
}

void tig_free_ifd(tig_tiff_ifd *v){
    if (!v) return;
    if (v->__opaque){
        free((void*)v->offsets);
        free((void*)v->sizes);
    }
    memset(v,0,sizeof(*v));
}

static int pread_all(int fd, void *buf, size_t n, uint64_t off){
#if defined(_WIN32)
    if (_lseeki64(fd, off, SEEK_SET)<0) return -1;
    size_t got=0; while (got<n){ int r=_read(fd,(char*)buf+got,(unsigned)(n-got)); if(r<=0)return -2; got+=(size_t)r; }
    return 0;
#else
    size_t got=0; while (got<n){ ssize_t r=pread(fd,(char*)buf+got,n-got,(off_t)off+got); if(r<=0)return -2; got+=(size_t)r; }
    return 0;
#endif
}
