#ifndef ZSTD_STATIC_LINKING_ONLY
#define ZSTD_STATIC_LINKING_ONLY
#endif

#include <stdio.h>
#include <stdlib.h>
#include <zstd.h>

static void checkSetParameter(ZSTD_CCtx_params* p, ZSTD_cParameter param, int value) {
  size_t r = ZSTD_CCtxParams_setParameter(p, param, value);
  if (ZSTD_isError(r)) {
    fprintf(stderr, "ZSTD_CCtxParams_setParameter: %s\n", ZSTD_getErrorName(r));
    exit(1);
  }
}

int main(void) {
  ZSTD_CCtx_params* p = ZSTD_createCCtxParams();
  checkSetParameter(p, ZSTD_c_srcSizeHint, 512u << 20);
  checkSetParameter(p, ZSTD_c_compressionLevel, 22);
  checkSetParameter(p, ZSTD_c_enableLongDistanceMatching, 1);
  size_t r = ZSTD_estimateCCtxSize_usingCCtxParams(p);
  if (ZSTD_isError(r)) {
    fprintf(stderr, "ZSTD_estimateCCtxSize_usingCCtxParams: %s\n", ZSTD_getErrorName(r));
    return 1;
  }
  fprintf(stdout, "estimated CCtx size: %zu\n", r);
  ZSTD_freeCCtxParams(p);
  return 0;
}
