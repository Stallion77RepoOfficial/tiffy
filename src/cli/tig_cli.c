#include "tigre.h"
#include "core/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void){
        fprintf(stderr,
            PROJECT_NAME " usage:\n"
            "  --clone --src <device> --dst <image> [--map map.json]\n"
            "  --scan  (--img <image> | --system <device>) --out <dir> [--max-hits N]\n"
            "  --dig   (--img <image> | --system <device>) --range <start:end> --out <dir>\n");
}

int main(int argc, char **argv){
    printf("%s %s — by %s\n", PROJECT_NAME, PROJECT_VERSION, PROJECT_AUTHOR);
    if (argc < 2){ usage(); return 1; }

    if (strcmp(argv[1],"--clone")==0){
        const char *src=0,*dst=0,*map="map.json";
        for (int i=2;i<argc;i++){
            if (!strcmp(argv[i],"--src") && i+1<argc) src=argv[++i];
            else if (!strcmp(argv[i],"--dst") && i+1<argc) dst=argv[++i];
            else if (!strcmp(argv[i],"--map") && i+1<argc) map=argv[++i];
        }
        if (!src || !dst){ usage(); return 1; }
        return tig_clone_to_img(src,dst,map,1<<20,3,1);

    } else if (strcmp(argv[1],"--scan")==0){
        const char *source=0,*out="./out"; uint64_t maxh=1000; int src_mode=0;
        for (int i=2;i<argc;i++){
            if (!strcmp(argv[i],"--img") && i+1<argc){
                if (src_mode){ usage(); return 1; }
                source=argv[++i]; src_mode=1;
            }
            else if (!strcmp(argv[i],"--system") && i+1<argc){
                if (src_mode){ usage(); return 1; }
                source=argv[++i]; src_mode=2;
            }
            else if (!strcmp(argv[i],"--out") && i+1<argc) out=argv[++i];
            else if (!strcmp(argv[i],"--max-hits") && i+1<argc) maxh=strtoull(argv[++i],0,10);
        }
        if (!source){ usage(); return 1; }
        return tig_engine_scan_recover(source,out,0,0,maxh,1);

    } else if (strcmp(argv[1],"--dig")==0){
        const char *source=0,*out="./out"; uint64_t s=0,e=0; int src_mode=0;
        for (int i=2;i<argc;i++){
            if (!strcmp(argv[i],"--img") && i+1<argc){
                if (src_mode){ usage(); return 1; }
                source=argv[++i]; src_mode=1;
            }
            else if (!strcmp(argv[i],"--system") && i+1<argc){
                if (src_mode){ usage(); return 1; }
                source=argv[++i]; src_mode=2;
            }
            else if (!strcmp(argv[i],"--out") && i+1<argc) out=argv[++i];
            else if (!strcmp(argv[i],"--range") && i+1<argc){
                char *tok = strtok(argv[++i],":");
                if(tok) s = strtoull(tok,0,10);
                tok = strtok(0,":");
                if(tok) e = strtoull(tok,0,10);
            }
        }
        if (!source || !e){ usage(); return 1; }
        tig_range r={s,e};
        return tig_engine_dig_range(source,r,out,0,1);
    }

    usage();
    return 1;
}
