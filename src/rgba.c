#include "../include/rgba.h"

static SDL_PixelFormat *fmt = NULL;

void rgba_init()
{
    fmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
}

uint32_t RGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return SDL_MapRGBA(fmt, r, g, b, a);
}