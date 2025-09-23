#ifndef TIG_LOG_H
#define TIG_LOG_H
#include <stdio.h>
#include <stdarg.h>

static inline void tig_logf(const char *lvl, const char *fmt, ...){
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[%s] ", lvl);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}
#define LOGI(...) tig_logf("INFO", __VA_ARGS__)
#define LOGW(...) tig_logf("WARN", __VA_ARGS__)
#define LOGE(...) tig_logf("ERR",  __VA_ARGS__)

#endif
