#include "../include/file.h"
#include "../external/stb_image/stb_image_write.h"
#include "../external/stb_image/stb_image.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

/* Internal canvas format (matches cui.cpp macros):
 *   bits 31-24 = R
 *   bits 23-16 = G
 *   bits 15- 8 = B
 *   bits  7- 0 = A
 *
 * stb_image_write expects packed RGBA bytes (R at lowest address).
 * On a little-endian machine that means the uint32 must be 0xAABBGGRR,
 * i.e. we swap R<->A and G<->B relative to our internal word.
 */

/* internal RGBА word  →  stb "RGBA bytes" word */
static inline uint32_t internal_to_stb(uint32_t c)
{
    uint8_t r = (c >> 24) & 0xFF;
    uint8_t g = (c >> 16) & 0xFF;
    uint8_t b = (c >>  8) & 0xFF;
    uint8_t a = (c      ) & 0xFF;
    /* stb wants R at byte-0 (lowest addr) = least-significant on LE */
    return (uint32_t)r        |
    (uint32_t)g <<  8  |
    (uint32_t)b << 16  |
    (uint32_t)a << 24;
}

static inline uint32_t *swizzle_copy(const uint32_t *pixels, int count)
{
    uint32_t *out = malloc((size_t)count * sizeof(uint32_t));
    if (!out) return NULL;
    for (int i = 0; i < count; i++)
        out[i] = internal_to_stb(pixels[i]);
    return out;
}

void export_png(const char *filename, uint32_t *canvas, int width, int height)
{
    uint32_t *tmp = swizzle_copy(canvas, width * height);
    if (!tmp) return;
    stbi_write_png(filename, width, height, 4, tmp, width * 4);
    free(tmp);
}

void export_jpg(const char *filename, uint32_t *canvas, int width, int height)
{
    uint32_t *tmp = swizzle_copy(canvas, width * height);
    if (!tmp) return;
    stbi_write_jpg(filename, width, height, 4, tmp, 90);
    free(tmp);
}

void export_bmp(const char *filename, uint32_t *canvas, int width, int height)
{
    uint32_t *tmp = swizzle_copy(canvas, width * height);
    if (!tmp) return;
    stbi_write_bmp(filename, width, height, 4, tmp);
    free(tmp);
}

uint32_t *load_image(const char *filename, int *out_width, int *out_height)
{
    int w, h, channels;
    uint8_t *data = stbi_load(filename, &w, &h, &channels, 4);
    if (!data)
    {
        fprintf(stderr, "Failed to load image: %s\n", filename);
        return NULL;
    }

    uint32_t *canvas = malloc((size_t)w * h * sizeof(uint32_t));
    if (!canvas)
    {
        stbi_image_free(data);
        return NULL;
    }

    for (int i = 0; i < w * h; i++)
    {
        uint8_t r = data[i * 4 + 0];
        uint8_t g = data[i * 4 + 1];
        uint8_t b = data[i * 4 + 2];
        uint8_t a = data[i * 4 + 3];
        /* pack into internal format: R=31-24, G=23-16, B=15-8, A=7-0 */
        canvas[i] = ((uint32_t)r << 24) |
        ((uint32_t)g << 16) |
        ((uint32_t)b <<  8) |
        (uint32_t)a;
    }

    stbi_image_free(data);
    *out_width  = w;
    *out_height = h;
    return canvas;
}
