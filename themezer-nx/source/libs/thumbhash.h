#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int ThumbHashToRGBA(const uint8_t *hash, size_t hashLen, int maxSize, uint8_t **rgba, int *width, int *height);

/* The ThumbHash of an RGBA picture (no more than 100x100 is needed) into hash, which has room for
 * THUMBHASH_MAX_LENGTH bytes. 0 when it can't be made. */
#define THUMBHASH_MAX_LENGTH 32
int RGBAToThumbHash(int w, int h, const uint8_t *rgba, uint8_t *hash, size_t *hashLen);

#ifdef __cplusplus
}
#endif
