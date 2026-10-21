/*
 * ThumbHash decoder adapted from datocms/fast_thumbhash, and an encoder ported from Evan Wallace's
 * reference implementation's rgbaToThumbHash (MIT License, Copyright (c) 2023 Evan Wallace).
 *
 * Original ThumbHash algorithm by Evan Wallace:
 * https://github.com/evanw/thumbhash
 *
 * Adapted from:
 * https://github.com/datocms/fast_thumbhash
 *
 * MIT License
 *
 * Copyright (c) 2023 Stefano Verna
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "thumbhash.h"

#include <math.h>
#include <stdlib.h>

static double GetAspectRatio(const uint8_t *hash){
    int hasAlpha = (hash[2] & 0x80) != 0;
    int major = hasAlpha ? 5 : 7;
    int minor = hash[3] & 7;
    int isLandscape = (hash[4] & 0x80) != 0;
    int lx = isLandscape ? major : minor;
    int ly = isLandscape ? minor : major;

    return (double)lx / (double)ly;
}

static int ClampByte(double value){
    if (value < 0.0)
        return 0;
    if (value > 1.0)
        return 255;
    return (int)lround(value * 255.0);
}

static double *DecodeChannel(int nx, int ny, double scale, const uint8_t *hash, int acStart, int *acIndex){
    double *ac = calloc((size_t)nx * (size_t)ny, sizeof(double));
    int outIndex = 0;

    if (!ac)
        return NULL;

    for (int cy = 0; cy < ny; cy++){
        for (int cx = cy ? 0 : 1; cx * ny < nx * (ny - cy); cx++){
            /* the index read once: reading it and moving it on in one expression is undefined */
            int index = (*acIndex)++;
            double value = (hash[acStart + (index >> 1)] >> ((index & 1) << 2)) & 15;
            ac[outIndex++] = (value / 7.5 - 1.0) * scale;
        }
    }

    return ac;
}

int ThumbHashToRGBA(const uint8_t *hash, size_t hashLen, int maxSize, uint8_t **rgba, int *width, int *height){
    if (!hash || !rgba || !width || !height || hashLen < 5 || maxSize <= 0)
        return 0;

    double ratio = GetAspectRatio(hash);
    *width = (int)lround(ratio > 1.0 ? maxSize : maxSize * ratio);
    *height = (int)lround(ratio > 1.0 ? maxSize / ratio : maxSize);

    if (*width < 1)
        *width = 1;
    if (*height < 1)
        *height = 1;

    uint32_t header24 = hash[0] | (hash[1] << 8) | (hash[2] << 16);
    uint32_t header16 = hash[3] | (hash[4] << 8);
    double lDc = (double)(header24 & 63) / 63.0;
    double pDc = (double)((header24 >> 6) & 63) / 31.5 - 1.0;
    double qDc = (double)((header24 >> 12) & 63) / 31.5 - 1.0;
    double lScale = (double)((header24 >> 18) & 31) / 31.0;
    int hasAlpha = (header24 >> 23) != 0;
    double pScale = (double)((header16 >> 3) & 63) / 63.0;
    double qScale = (double)((header16 >> 9) & 63) / 63.0;
    int isLandscape = (header16 >> 15) != 0;
    int lx = isLandscape ? (hasAlpha ? 5 : 7) : (header16 & 7);
    int ly = isLandscape ? (header16 & 7) : (hasAlpha ? 5 : 7);
    double aDc = hasAlpha && hashLen >= 6 ? (double)(hash[5] & 15) / 15.0 : 1.0;
    double aScale = hasAlpha && hashLen >= 6 ? (double)(hash[5] >> 4) / 15.0 : 0.0;
    int acStart = hasAlpha ? 6 : 5;
    int acIndex = 0;

    if (lx < 3)
        lx = 3;
    if (ly < 3)
        ly = 3;
    if (hasAlpha && hashLen < 6)
        return 0;

    double *lAc = DecodeChannel(lx, ly, lScale, hash, acStart, &acIndex);
    double *pAc = DecodeChannel(3, 3, pScale * 1.25, hash, acStart, &acIndex);
    double *qAc = DecodeChannel(3, 3, qScale * 1.25, hash, acStart, &acIndex);
    double *aAc = hasAlpha ? DecodeChannel(5, 5, aScale, hash, acStart, &acIndex) : NULL;
    uint8_t *pixels = malloc((size_t)(*width) * (size_t)(*height) * 4);

    if (!lAc || !pAc || !qAc || !pixels || (hasAlpha && !aAc)){
        free(lAc);
        free(pAc);
        free(qAc);
        free(aAc);
        free(pixels);
        return 0;
    }

    double fx[7] = {0};
    double fy[7] = {0};
    size_t out = 0;
    for (int y = 0; y < *height; y++){
        double py = ((double)y + 0.5) / *height;
        for (int x = 0; x < *width; x++){
            double px = ((double)x + 0.5) / *width;
            double l = lDc;
            double p = pDc;
            double q = qDc;
            double a = aDc;

            for (int cx = 0; cx < lx && cx < 7; cx++)
                fx[cx] = cos(M_PI * px * cx);
            for (int cy = 0; cy < ly && cy < 7; cy++)
                fy[cy] = cos(M_PI * py * cy);

            for (int cy = 0, j = 0; cy < ly; cy++){
                double fy2 = fy[cy] * 2.0;
                for (int cx = cy ? 0 : 1; cx * ly < lx * (ly - cy); cx++, j++)
                    l += lAc[j] * fx[cx] * fy2;
            }

            for (int cy = 0, j = 0; cy < 3; cy++){
                double fy2 = fy[cy] * 2.0;
                for (int cx = cy ? 0 : 1; cx < 3 - cy; cx++, j++){
                    double factor = fx[cx] * fy2;
                    p += pAc[j] * factor;
                    q += qAc[j] * factor;
                }
            }

            if (hasAlpha){
                for (int cy = 0, j = 0; cy < 5; cy++){
                    double fy2 = fy[cy] * 2.0;
                    for (int cx = cy ? 0 : 1; cx < 5 - cy; cx++, j++)
                        a += aAc[j] * fx[cx] * fy2;
                }
            }

            double b = l - 2.0 / 3.0 * p;
            double r = (3.0 * l - b + q) / 2.0;
            double g = r - q;

            pixels[out++] = ClampByte(r);
            pixels[out++] = ClampByte(g);
            pixels[out++] = ClampByte(b);
            pixels[out++] = ClampByte(a);
        }
    }

    free(lAc);
    free(pAc);
    free(qAc);
    free(aAc);

    *rgba = pixels;
    return 1;
}
/* The encoder, as evanw/thumbhash's rgbaToThumbHash. */

typedef struct {
    double dc;
    double ac[32]; /* at most 7x7 luminance factors, less the constant */
    int count;
    double scale;
} EncodedChannel;

/* Math.round's: for the values here, which aren't negative. */
static int JsRound(double value){
    return (int)floor(value + 0.5);
}

/* The DCT into the constant (DC) and the varying (AC) factors, these normalized to 0..1. */
static void EncodeChannel(const double *channel, int w, int h, int nx, int ny, double *fx, EncodedChannel *out){
    out->dc = 0.0;
    out->count = 0;
    out->scale = 0.0;
    for (int cy = 0; cy < ny; cy++){
        for (int cx = 0; cx * ny < nx * (ny - cy); cx++){
            double f = 0.0;
            for (int x = 0; x < w; x++)
                fx[x] = cos(M_PI / w * cx * (x + 0.5));
            for (int y = 0; y < h; y++){
                double fy = cos(M_PI / h * cy * (y + 0.5));
                for (int x = 0; x < w; x++)
                    f += channel[x + y * w] * fx[x] * fy;
            }
            f /= (double)w * h;
            if (cx || cy){
                out->ac[out->count++] = f;
                if (fabs(f) > out->scale)
                    out->scale = fabs(f);
            } else {
                out->dc = f;
            }
        }
    }
    if (out->scale > 0.0)
        for (int i = 0; i < out->count; i++)
            out->ac[i] = 0.5 + 0.5 / out->scale * out->ac[i];
}

int RGBAToThumbHash(int w, int h, const uint8_t *rgba, uint8_t *hash, size_t *hashLen){
    if (!rgba || !hash || !hashLen || w <= 0 || h <= 0)
        return 0;
    size_t count = (size_t)w * (size_t)h;

    /* the average colour */
    double avgR = 0.0, avgG = 0.0, avgB = 0.0, avgA = 0.0;
    for (size_t i = 0, j = 0; i < count; i++, j += 4){
        double alpha = rgba[j + 3] / 255.0;
        avgR += alpha / 255.0 * rgba[j];
        avgG += alpha / 255.0 * rgba[j + 1];
        avgB += alpha / 255.0 * rgba[j + 2];
        avgA += alpha;
    }
    if (avgA > 0.0){
        avgR /= avgA;
        avgG /= avgA;
        avgB /= avgA;
    }

    int hasAlpha = avgA < (double)count;
    int lLimit = hasAlpha ? 5 : 7; /* fewer luminance factors where there's alpha */
    int longest = w > h ? w : h;
    int lx = JsRound((double)lLimit * w / longest);
    int ly = JsRound((double)lLimit * h / longest);
    if (lx < 1)
        lx = 1;
    if (ly < 1)
        ly = 1;

    /* RGBA to LPQA (luminance, yellow-blue, red-green, alpha), over the average colour */
    double *channels = malloc(count * 4 * sizeof(double));
    double *fx = malloc((size_t)w * sizeof(double));
    if (!channels || !fx){
        free(channels);
        free(fx);
        return 0;
    }
    double *l = channels, *p = channels + count, *q = channels + 2 * count, *a = channels + 3 * count;
    for (size_t i = 0, j = 0; i < count; i++, j += 4){
        double alpha = rgba[j + 3] / 255.0;
        double r = avgR * (1.0 - alpha) + alpha / 255.0 * rgba[j];
        double g = avgG * (1.0 - alpha) + alpha / 255.0 * rgba[j + 1];
        double b = avgB * (1.0 - alpha) + alpha / 255.0 * rgba[j + 2];
        l[i] = (r + g + b) / 3.0;
        p[i] = (r + g) / 2.0 - b;
        q[i] = r - g;
        a[i] = alpha;
    }

    EncodedChannel lc, pc, qc, ac = {0};
    EncodeChannel(l, w, h, lx > 3 ? lx : 3, ly > 3 ? ly : 3, fx, &lc);
    EncodeChannel(p, w, h, 3, 3, fx, &pc);
    EncodeChannel(q, w, h, 3, 3, fx, &qc);
    if (hasAlpha)
        EncodeChannel(a, w, h, 5, 5, fx, &ac);
    free(channels);
    free(fx);

    /* the constants */
    int isLandscape = w > h;
    uint32_t header24 = (uint32_t)JsRound(63.0 * lc.dc) | ((uint32_t)JsRound(31.5 + 31.5 * pc.dc) << 6) |
                        ((uint32_t)JsRound(31.5 + 31.5 * qc.dc) << 12) | ((uint32_t)JsRound(31.0 * lc.scale) << 18) |
                        ((uint32_t)hasAlpha << 23);
    uint32_t header16 = (uint32_t)(isLandscape ? ly : lx) | ((uint32_t)JsRound(63.0 * pc.scale) << 3) |
                        ((uint32_t)JsRound(63.0 * qc.scale) << 9) | ((uint32_t)isLandscape << 15);
    for (int i = 0; i < THUMBHASH_MAX_LENGTH; i++)
        hash[i] = 0;
    hash[0] = (uint8_t)(header24 & 255);
    hash[1] = (uint8_t)((header24 >> 8) & 255);
    hash[2] = (uint8_t)(header24 >> 16);
    hash[3] = (uint8_t)(header16 & 255);
    hash[4] = (uint8_t)(header16 >> 8);
    int acStart = hasAlpha ? 6 : 5;
    if (hasAlpha)
        hash[5] = (uint8_t)(JsRound(15.0 * ac.dc) | (JsRound(15.0 * ac.scale) << 4));

    /* the varying factors, 4 bits each */
    const EncodedChannel *order[4] = {&lc, &pc, &qc, &ac};
    int acIndex = 0;
    for (int c = 0; c < (hasAlpha ? 4 : 3); c++)
        for (int i = 0; i < order[c]->count; i++, acIndex++)
            hash[acStart + (acIndex >> 1)] |= (uint8_t)(JsRound(15.0 * order[c]->ac[i]) << ((acIndex & 1) << 2));
    *hashLen = (size_t)acStart + (size_t)(acIndex + 1) / 2;
    return 1;
}
