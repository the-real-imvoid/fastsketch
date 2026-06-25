#include "../include/selection.h"
#include <stdlib.h>
#include <string.h>

uint32_t *selection = NULL;
uint32_t *clipboard = NULL;
int sel_width = 200;
int sel_height = 200;
int clip_height = 0;
int clip_width = 0;
int sel_x = 100;
int sel_y = 100;
int ants_offset = 0;
int timer = 0;
SDL_Texture *selection_tex = NULL;

static inline void draw_dashed_line(SDL_Renderer *r,
                                    int x0, int y0,
                                    int x1, int y1,
                                    int offset)
{
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);

    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;

    int err = dx - dy;

    int i = 0;

    while (1)
    {
        if (((i + offset) / 4) % 2 == 0)
        {
            SDL_SetRenderDrawColor(r, 0, 255, 255, 255);
            SDL_RenderDrawPoint(r, x0, y0);
        }
        else
        {
            SDL_SetRenderDrawColor(r, 0, 100, 100, 255);
            SDL_RenderDrawPoint(r, x0, y0);
        }

        if (x0 == x1 && y0 == y1)
            break;

        int e2 = 2 * err;

        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }

        i++;
    }
}

static inline void update_selection_texture(SDL_Renderer *renderer)
{
    if (selection_tex)
        SDL_DestroyTexture(selection_tex);

    selection_tex =
    SDL_CreateTexture(renderer,
                      SDL_PIXELFORMAT_RGBA8888,
                      SDL_TEXTUREACCESS_STREAMING,
                      sel_width,
                      sel_height);

    SDL_SetTextureBlendMode(selection_tex, SDL_BLENDMODE_BLEND);
}

void init_selection_tex_from_buffer(SDL_Renderer *renderer)
{
    update_selection_texture(renderer);
    if (selection_tex && selection)
        SDL_UpdateTexture(selection_tex, NULL, selection,
                          sel_width * sizeof(uint32_t));
}

static inline void safe_plot(uint32_t *canvas, int width, int height, int x, int y, uint32_t c)
{
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    canvas[y * width + x] = c;
}

void make_selection(int x1, int y1, int x2, int y2, uint32_t *canvas, int width, SDL_Renderer *renderer)
{
    int min_x = (x1 < x2) ? x1 : x2;
    int min_y = (y1 < y2) ? y1 : y2;

    sel_width  = abs(x2 - x1);
    sel_height = abs(y2 - y1);

    if (sel_width <= 0 || sel_height <= 0)
    {
        free(selection);
        selection = NULL;
        return;
    }

    sel_x = min_x;
    sel_y = min_y;

    size_t total = (size_t)sel_width * (size_t)sel_height;

    free(selection);
    selection = NULL;

    selection = malloc(total * sizeof(uint32_t));
    if (!selection) return;

    for (int y = 0; y < sel_height; y++)
        for (int x = 0; x < sel_width; x++)
            selection[y * sel_width + x] =
            canvas[(min_y + y) * width + (min_x + x)];

    update_selection_texture(renderer);
    SDL_UpdateTexture(selection_tex, NULL, selection,
                      sel_width * sizeof(uint32_t));
}

void place_selection(int x, int y, uint32_t *canvas, int width, int height)
{
    for (int dy = 0; dy < sel_height; dy++)
        for (int dx = 0; dx < sel_width; dx++)
            safe_plot(canvas, width, height, x+dx, y+dy, selection[dy*sel_width+dx]);
}

void yeet_old_area(uint32_t *canvas, int width, int height)
{
    for (int y = sel_y; y < sel_y + sel_height; y++)
        for (int x = sel_x; x < sel_x + sel_width; x++)
            safe_plot(canvas, width, height, x, y, 0xFFFFFF00);
}

void draw_preview(SDL_Renderer *renderer, int x, int y, float zoom, int cam_x, int cam_y)
{
    if (!selection || !selection_tex) return;

    SDL_Rect dst = {
        .x = (int)(x * zoom) + cam_x,
        .y = (int)(y * zoom) + cam_y,
        .w = (int)(sel_width * zoom),
        .h = (int)(sel_height * zoom)
    };

    SDL_RenderCopy(renderer, selection_tex, NULL, &dst);
}

void draw_marching_ants(SDL_Renderer *r, float zoom, int cam_x, int cam_y)
{
    if (sel_width <= 0 || sel_height <= 0)
        return;

    int screen_x0 = (int)(sel_x * zoom) + cam_x;
    int screen_y0 = (int)(sel_y * zoom) + cam_y;

    int screen_w = (int)(sel_width * zoom);
    int screen_h = (int)(sel_height * zoom);

    int screen_x1 = screen_x0 + screen_w - 1;
    int screen_y1 = screen_y0 + screen_h - 1;
    draw_dashed_line(r, screen_x0, screen_y0, screen_x1, screen_y0, ants_offset);
    draw_dashed_line(r, screen_x1, screen_y0, screen_x1, screen_y1, ants_offset);
    draw_dashed_line(r, screen_x1, screen_y1, screen_x0, screen_y1, ants_offset);
    draw_dashed_line(r, screen_x0, screen_y1, screen_x0, screen_y0, ants_offset);
}

void draw_marching_ants_specific(SDL_Renderer *r, float zoom, int cam_x, int cam_y, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    int screen_x0 = (int)(x * zoom) + cam_x;
    int screen_y0 = (int)(y * zoom) + cam_y;

    int screen_w = (int)(w * zoom);
    int screen_h = (int)(h * zoom);

    int screen_x1 = screen_x0 + screen_w - 1;
    int screen_y1 = screen_y0 + screen_h - 1;
    draw_dashed_line(r, screen_x0, screen_y0, screen_x1, screen_y0, ants_offset);
    draw_dashed_line(r, screen_x1, screen_y0, screen_x1, screen_y1, ants_offset);
    draw_dashed_line(r, screen_x1, screen_y1, screen_x0, screen_y1, ants_offset);
    draw_dashed_line(r, screen_x0, screen_y1, screen_x0, screen_y0, ants_offset);
}

void step_ant_state(void)
{
    timer++;
    if (timer > 4)
    {
        ants_offset = (ants_offset + 1) % 8;
        timer = 0;
    }
}

void free_selection(void)
{
    free(selection);
    selection = NULL;

    if (selection_tex)
        SDL_DestroyTexture(selection_tex);

    selection_tex = NULL;
}

void free_clipboard(void)
{
    free(clipboard);
    clipboard = NULL;
}

void copy(void)
{
    if (!selection || sel_width <= 0 || sel_height <= 0) return;
    size_t bytes = (size_t)sel_width * (size_t)sel_height * sizeof(uint32_t);
    free(clipboard);
    clipboard = malloc(bytes);
    if (!clipboard) return;
    memcpy(clipboard, selection, bytes);
    clip_width = sel_width;
    clip_height = sel_height;
}
