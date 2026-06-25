#include "../include/cursor.h"
#include <SDL2/SDL_image.h>

#define R(c) (((c) >> 24) & 0xFF)
#define G(c) (((c) >> 16) & 0xFF)
#define B(c) (((c) >> 8 ) & 0xFF)
#define A(c) (((c)      ) & 0xFF)

SDL_Surface *fill_cursor;
SDL_Texture *bucket_tex;

void cursor_init(SDL_Renderer *renderer)
{
    fill_cursor = IMG_Load("../assets/cursors/fillbucket.png");
    if (!fill_cursor)
    {
        printf("IMG_Load failed: %s\n", IMG_GetError());
        return;
    }
    bucket_tex = SDL_CreateTextureFromSurface(renderer, fill_cursor);
    if (!bucket_tex)
    {
        printf("SDL_CreateTextureFromSurface failed: %s\n", SDL_GetError());
    }
    SDL_FreeSurface(fill_cursor);
}

void render_brush_cursor(int gx, int gy, int radius,
                          SDL_Renderer *renderer, float zoom,
                          uint32_t *canvas, int cx, int cy,
                          int width, int camera_x, int camera_y)
{
    if (radius == 0)
    {
        // 1. Sample canvas color directly
        uint32_t pix_clr = canvas[cy * width + cx];
        uint8_t inv_r = 255 - R(pix_clr);
        uint8_t inv_g = 255 - G(pix_clr);
        uint8_t inv_b = 255 - B(pix_clr);

        SDL_SetRenderDrawColor(renderer, 0, 255, 255, 100);
        SDL_Rect snap_rect;
        snap_rect.x = (int)(cx * zoom + camera_x)+1;
        snap_rect.y = (int)(cy * zoom + camera_y)+1;
        snap_rect.w = (int)zoom;
        snap_rect.h = (int)zoom;
        SDL_RenderFillRect(renderer, &snap_rect);

        return;
    }
    radius = (int)(radius * zoom);
    int x = radius;
    int y = 0;
    // correct midpoint circle decision parameter
    int err = 1 - radius;

    uint32_t pix_clr = canvas[cy * width + cx];

    uint8_t inv_r = 255 - R(pix_clr);
    uint8_t inv_g = 255 - G(pix_clr);
    uint8_t inv_b = 255 - B(pix_clr);
    SDL_SetRenderDrawColor(renderer, inv_r, inv_g, inv_b, 255);

    while (x >= y)
    {
        SDL_RenderDrawPoint(renderer, gx + x, gy + y);
        SDL_RenderDrawPoint(renderer, gx + y, gy + x);
        SDL_RenderDrawPoint(renderer, gx - y, gy + x);
        SDL_RenderDrawPoint(renderer, gx - x, gy + y);
        SDL_RenderDrawPoint(renderer, gx - x, gy - y);
        SDL_RenderDrawPoint(renderer, gx - y, gy - x);
        SDL_RenderDrawPoint(renderer, gx + y, gy - x);
        SDL_RenderDrawPoint(renderer, gx + x, gy - y);
        y++;
        if (err < 0)
        {
            err += 2 * y + 1;
        }
        else
        {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void render_fill_cursor(int gx, int gy, SDL_Renderer *renderer)
{
    SDL_Rect dst = {gx-20, gy, 20, 20};
    SDL_RenderCopy(renderer, bucket_tex, NULL, &dst);
}
