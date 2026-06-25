#include "../include/shapes.h"
#include "../include/drawops.h"
#include "../include/rgba.h"
#include <stdbool.h>
#include <stdint.h>

#define R(c) (((c) >> 24) & 0xFF)
#define G(c) (((c) >> 16) & 0xFF)
#define B(c) (((c) >> 8)  & 0xFF)
#define A(c) (((c)      ) & 0xFF)

static inline void set_sdl_color_u32(SDL_Renderer *r, uint32_t c)
{
    SDL_SetRenderDrawColor(r, R(c), G(c), B(c), A(c));
}

static inline bool in_bounds(int x, int y, int width, int height)
{
    return x >= 0 && x < width && y >= 0 && y < height;
}

static inline uint32_t blend_pixel(uint32_t dst,
                                   uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa)
{
    if (sa == 255) return RGBA(sr, sg, sb, 255);
    uint16_t inv_a = 255 - sa;
    uint8_t out_r = (uint8_t)((sr * sa + R(dst) * inv_a) / 255);
    uint8_t out_g = (uint8_t)((sg * sa + G(dst) * inv_a) / 255);
    uint8_t out_b = (uint8_t)((sb * sa + B(dst) * inv_a) / 255);
    return RGBA(out_r, out_g, out_b, 255);
}

static inline void safe_plot(uint32_t *canvas, int x, int y,
                             int width, int height, uint32_t color)
{
    if (in_bounds(x, y, width, height))
        canvas[y * width + x] = color;
}

static inline void fill_span(uint32_t *row, int x_start, int x_end,
                             uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa,
                             uint32_t packed,
                             int width)
{
    if (x_start >= x_end) return;

    if (x_start < 0) x_start = 0;
    if (x_end > width) x_end = width;

    for (int x = x_start; x < x_end; x++)
        row[x] = row[x] = blend_pixel(row[x], sr, sg, sb, sa);
}

static void draw_circle(SDL_Renderer *renderer, int cx, int cy, int r, uint32_t c)
{
    if (r <= 0)
    {
        set_sdl_color_u32(renderer, c);
        SDL_RenderDrawPoint(renderer, cx, cy);
        return;
    }
    set_sdl_color_u32(renderer, c);
    int x = r, y = 0, err = 1 - r;
    while (x >= y)
    {
        for (int i = cx - x; i <= cx + x; i++)
        {
            SDL_RenderDrawPoint(renderer, i, cy + y);
            SDL_RenderDrawPoint(renderer, i, cy - y);
        }
        for (int i = cx - y; i <= cx + y; i++)
        {
            SDL_RenderDrawPoint(renderer, i, cy + x);
            SDL_RenderDrawPoint(renderer, i, cy - x);
        }
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}


static void compute_ellipse_bounds(int rx, int ry, int *max_x)
{
    for (int i = 0; i <= ry; i++) max_x[i] = 0;

    int64_t rx2 = (int64_t)rx * rx;
    int64_t ry2 = (int64_t)ry * ry;

    int x = 0, y = ry;
    int64_t p = ry2 - rx2 * ry + rx2 / 4;
    int64_t dx = 2 * ry2 * x;
    int64_t dy = 2 * rx2 * y;

    while (dx < dy)
    {
        if (y >= 0 && y <= ry && x > max_x[y]) max_x[y] = x;
        x++;
        dx += 2 * ry2;
        if (p < 0)
        {
            p += dx + ry2;
        }
        else
        {
            y--;
            dy -= 2 * rx2;
            p += dx - dy + ry2;
        }
    }

    p = ry2 * (int64_t)(x * x) + (int64_t)ry2 / 4 - ry2 * rx2 + rx2 * (int64_t)((y - 1) * (y - 1));

    while (y >= 0)
    {
        if (y >= 0 && y <= ry && x > max_x[y]) max_x[y] = x;
        y--;
        dy -= 2 * rx2;
        if (p > 0)
        {
            p += rx2 - dy;
        }
        else
        {
            x++;
            dx += 2 * ry2;
            p += dx - dy + rx2;
        }
    }
}

void put_oval(uint32_t *canvas,
              int cx, int cy,
              int rx, int ry,
              uint32_t fill_color,
              uint32_t outline_color,
              int outline_thickness,
              int width, int height)
{
    if (rx <= 0 || ry <= 0) return;

    uint8_t or_ = R(outline_color), og = G(outline_color), ob = B(outline_color), oa = A(outline_color);
    uint8_t fr  = R(fill_color),    fg = G(fill_color),    fb = B(fill_color),    fa = A(fill_color);

    int *max_x_outer = malloc((ry + 1) * sizeof(int));
    compute_ellipse_bounds(rx, ry, max_x_outer);

    int inner_rx = rx - outline_thickness;
    int inner_ry = ry - outline_thickness;
    int *max_x_inner = NULL;

    if (inner_rx > 0 && inner_ry > 0)
    {
        max_x_inner = calloc((ry + 1), sizeof(int));
        compute_ellipse_bounds(inner_rx, inner_ry, max_x_inner);
    }

    for (int y_offset = -ry; y_offset <= ry; y_offset++)
    {
        int curr_y = cy + y_offset;
        if (curr_y < 0 || curr_y >= height) continue;

        uint32_t *row = canvas + curr_y * width;
        int abs_y = abs(y_offset);

        int x_outer = max_x_outer[abs_y];
        int x_inner = (max_x_inner && abs_y <= inner_ry) ? max_x_inner[abs_y] : 0;

        if (x_inner > 0)
        {
            fill_span(row, cx - x_outer, cx - x_inner, or_, og, ob, oa, outline_color, width);
            fill_span(row, cx - x_inner, cx + x_inner + 1, fr, fg, fb, fa, fill_color, width);
            fill_span(row, cx + x_inner + 1, cx + x_outer + 1, or_, og, ob, oa, outline_color, width);
        }
        else
        {
            fill_span(row, cx - x_outer, cx + x_outer + 1, or_, og, ob, oa, outline_color, width);
        }
    }

    free(max_x_outer);
    if (max_x_inner) free(max_x_inner);

    dirty_mark_rect(cx - rx - outline_thickness, cy - ry - outline_thickness,
                    cx + rx + outline_thickness, cy + ry + outline_thickness);
}


static inline int get_ellipse_x(int64_t rx, int64_t ry, int64_t y) {
    if (y > ry) return 0;
    if (ry == 0) return 0;
    double val = 1.0 - (double)(y * y) / (double)(ry * ry);
    if (val < 0.0) return 0;
    return (int)(rx * sqrt(val) + 0.5);
}

void preview_oval(SDL_Renderer *renderer,
                  int cx, int cy,
                  int rx, int ry,
                  int thickness,
                  uint32_t fill_color,
                  uint32_t outline_color)
{
    if (rx <= 0 || ry <= 0) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    int inner_rx = rx - thickness;
    int inner_ry = ry - thickness;

    for (int y_offset = -ry; y_offset <= ry; y_offset++)
    {
        int curr_y = cy + y_offset;
        int abs_y = abs(y_offset);

        int x_outer = get_ellipse_x(rx, ry, abs_y);

        int x_inner = (inner_rx > 0 && inner_ry > 0 && abs_y <= inner_ry)
        ? get_ellipse_x(inner_rx, inner_ry, abs_y)
        : 0;

        if (x_inner > 0)
        {
            set_sdl_color_u32(renderer, outline_color);
            SDL_RenderDrawLine(renderer, cx - x_outer, curr_y, cx - x_inner - 1, curr_y);

            set_sdl_color_u32(renderer, fill_color);
            SDL_RenderDrawLine(renderer, cx - x_inner, curr_y, cx + x_inner, curr_y);

            set_sdl_color_u32(renderer, outline_color);
            SDL_RenderDrawLine(renderer, cx + x_inner + 1, curr_y, cx + x_outer, curr_y);
        }
        else
        {
            set_sdl_color_u32(renderer, outline_color);
            SDL_RenderDrawLine(renderer, cx - x_outer, curr_y, cx + x_outer, curr_y);
        }
    }
}


void put_rect(uint32_t *canvas,
              int x1, int y1, int x2, int y2,
              int outline_thickness,
              uint32_t color1,
              uint32_t color2,
              int width,
              int height)
{
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= width)  x2 = width  - 1;
    if (y2 >= height) y2 = height - 1;
    if (x1 >= x2 || y1 >= y2) return;

    int t = outline_thickness;
    uint8_t or_ = R(color1), og = G(color1), ob = B(color1), oa = A(color1);
    uint8_t fr  = R(color2), fg = G(color2), fb = B(color2), fa = A(color2);
    bool has_fill = (t < (x2 - x1) / 2) && (t < (y2 - y1) / 2);

    for (int y = y1; y < y2; y++)
    {
        uint32_t *row = canvas + y * width;
        bool in_top    = (y < y1 + t);
        bool in_bottom = (y >= y2 - t);

        if (in_top || in_bottom)
        {
            fill_span(row, x1, x2, or_, og, ob, oa, color1, width);
        }
        else
        {
            int inner_x1 = x1 + t;
            int inner_x2 = x2 - t;
            fill_span(row, x1,       inner_x1, or_, og, ob, oa, color1, width);
            if (has_fill)
                fill_span(row, inner_x1, inner_x2, fr, fg, fb, fa, color2, width);
            fill_span(row, inner_x2, x2,       or_, og, ob, oa, color1, width);
        }
    }

    dirty_mark_rect(x1 - outline_thickness, y1 - outline_thickness,
                    x2 + outline_thickness, y2 + outline_thickness);
}


void draw_rect_preview(SDL_Renderer *renderer,
                       int x1, int y1, int x2, int y2,
                       int t,
                       uint32_t c,
                       uint32_t c2)
{
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect rect;

    // Interior fill.
    if (t < (x2 - x1) / 2 && t < (y2 - y1) / 2)
    {
        set_sdl_color_u32(renderer, c2);
        rect.x = x1 + t;
        rect.y = y1 + t;
        rect.w = (x2 - x1) - 2 * t;
        rect.h = (y2 - y1) - 2 * t;
        SDL_RenderFillRect(renderer, &rect);
    }

    set_sdl_color_u32(renderer, c);
    rect.x = x1; rect.y = y1; rect.w = x2 - x1; rect.h = t;
    SDL_RenderFillRect(renderer, &rect);
    rect.y = y2 - t;
    SDL_RenderFillRect(renderer, &rect);
    rect.x = x1; rect.y = y1; rect.w = t; rect.h = y2 - y1;
    SDL_RenderFillRect(renderer, &rect);
    rect.x = x2 - t;
    SDL_RenderFillRect(renderer, &rect);
}

void bresenham(int x1, int y1, int x2, int y2,
               uint32_t c, uint32_t *canvas,
               int width, int height, int t)
{
    int dx =  abs(x2 - x1);
    int sx = (x1 < x2) ? 1 : -1;
    int dy = -abs(y2 - y1);
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx + dy;

    while (1)
    {
        if (in_bounds(x1, y1, width, height))
            apply_brush_circle(x1, y1, t, c, canvas, width, height);

        if (x1 == x2 && y1 == y2) break;

        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }

    dirty_mark_rect(x1 - t, y1 - t, x2 + t, y2 + t);
}

void preview_line(SDL_Renderer *renderer,
                  int x0, int y0,
                  int x1, int y1,
                  int thickness,
                  uint32_t c)
{
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int steps = dx > dy ? dx : dy;
    int radius = thickness / 2;

    if (steps == 0)
    {
        draw_circle(renderer, x0, y0, radius, c);
        return;
    }

    float xInc = (float)(x1 - x0) / steps;
    float yInc = (float)(y1 - y0) / steps;
    float x = (float)x0;
    float y = (float)y0;

    for (int i = 0; i <= steps; i++)
    {
        draw_circle(renderer, (int)x, (int)y, radius, c);
        x += xInc;
        y += yInc;
    }
}
