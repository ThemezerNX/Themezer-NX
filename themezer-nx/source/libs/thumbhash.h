#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int ThumbHashToRGBA(const uint8_t *hash, size_t hashLen, int maxSize, uint8_t **rgba, int *width, int *height);

#ifdef __cplusplus
}
#endif
