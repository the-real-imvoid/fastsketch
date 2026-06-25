#include "../include/drawops.h"
#include "../include/undo.h"
#include "../include/rgba.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define R(c) (((c) >> 24) & 0xFF)
#define G(c) (((c) >> 16) & 0xFF)
#define B(c) (((c) >> 8)  & 0xFF)
#define A(c) (((c)      ) & 0xFF)

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static int width;
static int height;

/* ---- dirty rect ---- */
static SDL_Rect g_dirty;
static bool     g_dirty_valid = false;

static bool *g_stroke_mask = NULL;
static int g_mask_width = 0;
static int g_mask_height = 0;

void start_stroke(int width, int height)
{
    if (!g_stroke_mask || g_mask_width != width || g_mask_height != height)
    {
        g_stroke_mask = realloc(g_stroke_mask, width * height * sizeof(bool));
        g_mask_width = width;
        g_mask_height = height;
    }

    memset(g_stroke_mask, 0, width * height * sizeof(bool));
}

static inline void blend_canvas_pixel(uint32_t *canvas, int x, int y, int width, uint32_t color)
{
    uint32_t *dst = &canvas[y * width + x];

    uint8_t sa = color & 0xFF;
    if (sa == 255) {
        *dst = color;
        return;
    }
    if (sa == 0) return;

    uint8_t sr = (color >> 24) & 0xFF;
    uint8_t sg = (color >> 16) & 0xFF;
    uint8_t sb = (color >> 8)  & 0xFF;

    uint8_t dr = (*dst >> 24) & 0xFF;
    uint8_t dg = (*dst >> 16) & 0xFF;
    uint8_t db = (*dst >> 8)  & 0xFF;

    uint16_t inv_a = 255 - sa;
    uint8_t out_r = (uint8_t)((sr * sa + dr * inv_a) / 255);
    uint8_t out_g = (uint8_t)((sg * sa + dg * inv_a) / 255);
    uint8_t out_b = (uint8_t)((sb * sa + db * inv_a) / 255);

    *dst = (out_r << 24) | (out_g << 16) | (out_b << 8) | 255;
}

void dirty_reset(void)
{
    g_dirty_valid = false;
}

bool dirty_valid(void)
{
    return g_dirty_valid;
}

SDL_Rect dirty_get(void)
{
    return g_dirty;
}

static void dirty_expand(int x0, int y0, int x1, int y1)
{
    if (!g_dirty_valid)
    {
        g_dirty = (SDL_Rect){ x0, y0, x1 - x0, y1 - y0 };
        g_dirty_valid = true;
        return;
    }
    int left   = g_dirty.x < x0 ? g_dirty.x : x0;
    int top    = g_dirty.y < y0 ? g_dirty.y : y0;
    int right  = (g_dirty.x + g_dirty.w) > x1 ? (g_dirty.x + g_dirty.w) : x1;
    int bottom = (g_dirty.y + g_dirty.h) > y1 ? (g_dirty.y + g_dirty.h) : y1;
    g_dirty = (SDL_Rect){ left, top, right - left, bottom - top };
}

void dirty_mark_rect(int x1, int y1, int x2, int y2)
{
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    dirty_expand(x1, y1, x2, y2);
}

/* ---- canvas ops ---- */

void resize_canvas(int new_w, int new_h,
                   uint32_t **canvas,
                   SDL_Renderer *renderer,
                   SDL_Texture **texture)
{
    uint32_t *new_canvas = malloc((size_t)new_w * new_h * sizeof(uint32_t));
    if (!new_canvas) exit(1);

    for (int i = 0; i < new_w * new_h; i++)
        new_canvas[i] = 0xFFFFFFFF;

    int copy_w = new_w < width ? new_w : width;
    int copy_h = new_h < height ? new_h : height;

    for (int y = 0; y < copy_h; y++)
        memcpy(new_canvas + y * new_w,
               (*canvas) + y * width,
               copy_w * sizeof(uint32_t));

    free(*canvas);
    *canvas = new_canvas;

    width  = new_w;
    height = new_h;

    SDL_DestroyTexture(*texture);
    *texture = SDL_CreateTexture(renderer,
                                 SDL_PIXELFORMAT_RGBA8888,
                                 SDL_TEXTUREACCESS_STREAMING,
                                 width, height);
    SDL_SetTextureBlendMode(*texture, SDL_BLENDMODE_BLEND);

    drawops_set_size(width, height);
    take_snapshot(*canvas, width, height);
}

void reset_canvas(uint32_t *canvas, int w, int h)
{
    for (int i = 0; i < w * h; i++)
        canvas[i] = 0xFFFFFFFF;
}

void put_pixel(uint32_t pixels[], int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    pixels[y * width + x] = ((uint32_t)r << 24) | ((uint32_t)g << 16) |
    ((uint32_t)b <<  8) | a;
    dirty_expand(x, y, x + 1, y + 1);
}

void drawops_set_size(int w, int h)
{
    width  = w;
    height = h;
}

void apply_brush_circle(int cx, int cy, int radius,
                        uint32_t color,
                        uint32_t *canvas,
                        int width, int height)
{
    dirty_expand(cx - radius, cy - radius, cx + radius + 1, cy + radius + 1);

    int r2 = radius * radius;

    uint8_t sr = R(color);
    uint8_t sg = G(color);
    uint8_t sb = B(color);
    uint8_t sa = A(color);

    for (int y = -radius; y <= radius; y++)
    {
        for (int x = -radius; x <= radius; x++)
        {
            if (x*x + y*y > r2)
                continue;

            int px = cx + x;
            int py = cy + y;

            if (px < 0 || py < 0 || px >= width || py >= height)
                continue;

            int idx = py * width + px;

            uint8_t out_r = sr;
            uint8_t out_g = sg;
            uint8_t out_b = sb;
            uint8_t out_a = 255;

            if (sa == 255)
                goto draw;

            {
                uint32_t dst = canvas[idx];
                uint8_t dr = R(dst);
                uint8_t dg = G(dst);
                uint8_t db = B(dst);

                float sa_f = sa / 255.0f;
                out_r = (uint8_t)(sr * sa_f + dr * (1.0f - sa_f));
                out_g = (uint8_t)(sg * sa_f + dg * (1.0f - sa_f));
                out_b = (uint8_t)(sb * sa_f + db * (1.0f - sa_f));
                out_a = 255;
            }

            draw:
            canvas[idx] = ((uint32_t)out_r << 24) |
            ((uint32_t)out_g << 16) |
            ((uint32_t)out_b <<  8) |
            out_a;
        }
    }
}

typedef struct { int x, y; } Point;

void fill(int cx, int cy, int width, int height, uint32_t color, uint32_t *canvas)
{
    if (cx < 0 || cx >= width || cy < 0 || cy >= height)
        return;

    uint32_t old_color = canvas[cy * width + cx];
    if (old_color == color)
        return;

    Point *stack = malloc((size_t)width * height * sizeof(Point));
    if (!stack) return;
    int top = 0;

    stack[top++] = (Point){cx, cy};

    int min_x = cx, max_x = cx, min_y = cy, max_y = cy;

    while (top > 0)
    {
        Point p = stack[--top];
        int x = p.x;
        int y = p.y;

        int left = x;
        while (left >= 0 && canvas[y * width + left] == old_color)
            left--;
        left++;

        int right = x;
        while (right < width && canvas[y * width + right] == old_color)
            right++;
        right--;

        for (int i = left; i <= right; i++)
            canvas[y * width + i] = color;

        /* track bounding box for dirty rect */
        if (left  < min_x) min_x = left;
        if (right > max_x) max_x = right;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;

        if (y > 0)
        {
            int in_span = 0;
            for (int i = left; i <= right; i++)
            {
                if (canvas[(y - 1) * width + i] == old_color)
                {
                    if (!in_span) { stack[top++] = (Point){i, y - 1}; in_span = 1; }
                }
                else in_span = 0;
            }
        }
        if (y < height - 1)
        {
            int in_span = 0;
            for (int i = left; i <= right; i++)
            {
                if (canvas[(y + 1) * width + i] == old_color)
                {
                    if (!in_span) { stack[top++] = (Point){i, y + 1}; in_span = 1; }
                }
                else in_span = 0;
            }
        }
    }

    free(stack);
    dirty_expand(min_x, min_y, max_x + 1, max_y + 1);
}

void apply_airbrush(int cx, int cy, int radius, uint32_t color, uint32_t *canvas, int width, int height, int density)
{
    if (radius <= 0) {
        if (cx >= 0 && cx < width && cy >= 0 && cy < height) {
            blend_canvas_pixel(canvas, cx, cy, width, color);
        }
        return;
    }

    int particles = density * radius;

    for (int i = 0; i < particles; i++)
    {
        double angle = ((double)rand() / RAND_MAX) * 2.0 * M_PI;
        double r_factor = ((double)rand() / RAND_MAX);
        double r = r_factor * radius;

        int px = cx + (int)(r * cos(angle));
        int py = cy + (int)(r * sin(angle));

        if (px >= 0 && px < width && py >= 0 && py < height)
        {
            blend_canvas_pixel(canvas, px, py, width, color);
        }
    }

    dirty_mark_rect(cx - radius, cy - radius, cx + radius, cy + radius);
}

//mode 0 = burn
//mode 1 = dodge
void apply_brightness_tool(int cx, int cy, int radius, uint32_t *canvas,
                                         int width, int height, int mode, float amount)
{
    // Safety check: make sure the mask is initialized
    if (!g_stroke_mask) return;

    int r_start = (radius == 0) ? 0 : -radius;
    int r_end   = (radius == 0) ? 0 : radius;
    bool changed_anything = false;

    for (int ky = r_start; ky <= r_end; ky++)
    {
        for (int kx = r_start; kx <= r_end; kx++)
        {
            if (radius > 0 && (kx * kx + ky * ky > radius * radius)) {
                continue;
            }

            int px = cx + kx;
            int py = cy + ky;

            if (px >= 0 && px < width && py >= 0 && py < height)
            {
                int idx = py * width + px;

                // --- THE CRITICAL CHECK ---
                // If this pixel has already been altered during this drag, skip it!
                if (g_stroke_mask[idx]) {
                    continue;
                }

                // Mark it as modified so it can't be touched again until mouse release
                g_stroke_mask[idx] = true;
                changed_anything = true;

                uint32_t current_pixel = canvas[idx];

                float r = R(current_pixel) / 255.0f;
                float g = G(current_pixel) / 255.0f;
                float b = B(current_pixel) / 255.0f;
                uint8_t a = A(current_pixel);

                if (mode == 1)
                {
                    r += amount; g += amount; b += amount;
                }
                else
                {
                    float factor = 1.0f - amount;
                    r *= factor; g *= factor; b *= factor;
                }

                if (r > 1.0f) r = 1.0f; if (r < 0.0f) r = 0.0f;
                if (g > 1.0f) g = 1.0f; if (g < 0.0f) g = 0.0f;
                if (b > 1.0f) b = 1.0f; if (b < 0.0f) b = 0.0f;

                uint8_t out_r = (uint8_t)(r * 255.0f);
                uint8_t out_g = (uint8_t)(g * 255.0f);
                uint8_t out_b = (uint8_t)(b * 255.0f);

                canvas[idx] = (out_r << 24) | (out_g << 16) | (out_b << 8) | a;
            }
        }
    }

    if (changed_anything) {
        dirty_mark_rect(cx - radius, cy - radius, cx + radius, cy + radius);
    }
}


// Mode 0 = Blur, Mode 1 = Sharpen
void apply_filter_tool(int cx, int cy, int radius, uint32_t *canvas, int width, int height, int mode)
{
    if (radius <= 0) return;

    int x_start = cx - radius; if (x_start < 1) x_start = 1;
    int y_start = cy - radius; if (y_start < 1) y_start = 1;
    int x_end   = cx + radius; if (x_end >= width - 1)  x_end = width - 2;
    int y_end   = cy + radius; if (y_end >= height - 1) y_end = height - 2;

    int bbox_w = x_end - x_start + 1;
    int bbox_h = y_end - y_start + 1;
    if (bbox_w <= 0 || bbox_h <= 0) return;

    uint32_t *temp_buffer = malloc(bbox_w * bbox_h * sizeof(uint32_t));
    if (!temp_buffer) return;

    float effect_strength = (mode == 0) ? 0.40f : 0.01f;

    float kernel[3][3];
    if (mode == 0) { // Box Blur
        for(int i = 0; i < 3; i++)
            for(int j = 0; j < 3; j++) kernel[i][j] = 1.0f / 9.0f;
    } else {
        kernel[0][0] = -1.0f/8.0f; kernel[0][1] = -1.0f/8.0f; kernel[0][2] = -1.0f/8.0f;
        kernel[1][0] = -1.0f/8.0f; kernel[1][1] =  2.0f;       kernel[1][2] = -1.0f/8.0f;
        kernel[2][0] = -1.0f/8.0f; kernel[2][1] = -1.0f/8.0f; kernel[2][2] = -1.0f/8.0f;
    }

    for (int y = y_start; y <= y_end; y++)
    {
        for (int x = x_start; x <= x_end; x++)
        {
            int tx = x - x_start;
            int ty = y - y_start;

            uint32_t orig_pixel = canvas[y * width + x];

            int dx = x - cx;
            int dy = y - cy;
            if (dx*dx + dy*dy > radius*radius) {
                temp_buffer[ty * bbox_w + tx] = orig_pixel;
                continue;
            }

            float orig_r = (float)((orig_pixel >> 24) & 0xFF);
            float orig_g = (float)((orig_pixel >> 16) & 0xFF);
            float orig_b = (float)((orig_pixel >>  8) & 0xFF);
            uint8_t center_a = orig_pixel & 0xFF;

            float sum_r = 0.0f, sum_g = 0.0f, sum_b = 0.0f;

            for (int ky = -1; ky <= 1; ky++)
            {
                int ny = y + ky;
                if (ny < 0) ny = 0; if (ny >= height) ny = height - 1;

                for (int kx = -1; kx <= 1; kx++)
                {
                    int nx = x + kx;
                    if (nx < 0) nx = 0; if (nx >= width) nx = width - 1;

                    uint32_t neighbor = canvas[ny * width + nx];
                    float weight = kernel[ky + 1][kx + 1];

                    sum_r += ((neighbor >> 24) & 0xFF) * weight;
                    sum_g += ((neighbor >> 16) & 0xFF) * weight;
                    sum_b += ((neighbor >>  8) & 0xFF) * weight;
                }
            }

            sum_r = (sum_r * effect_strength) + (orig_r * (1.0f - effect_strength));
            sum_g = (sum_g * effect_strength) + (orig_g * (1.0f - effect_strength));
            sum_b = (sum_b * effect_strength) + (orig_b * (1.0f - effect_strength));

            if (sum_r < 0.0f) sum_r = 0.0f; if (sum_r > 255.0f) sum_r = 255.0f;
            if (sum_g < 0.0f) sum_g = 0.0f; if (sum_g > 255.0f) sum_g = 255.0f;
            if (sum_b < 0.0f) sum_b = 0.0f; if (sum_b > 255.0f) sum_b = 255.0f;

            temp_buffer[ty * bbox_w + tx] = ((uint8_t)sum_r << 24) |
            ((uint8_t)sum_g << 16) |
            ((uint8_t)sum_b <<  8) | center_a;
        }
    }

    for (int y = y_start; y <= y_end; y++)
    {
        for (int x = x_start; x <= x_end; x++)
        {
            canvas[y * width + x] = temp_buffer[(y - y_start) * bbox_w + (x - x_start)];
        }
    }

    free(temp_buffer);
    dirty_mark_rect(x_start, y_start, x_end + 1, y_end + 1);
}

void apply_smudge_tool(int cx, int cy, int last_cx, int last_cy, int radius, uint32_t *canvas, int width, int height, float strength)
{
    if (radius <= 0) return;

    int x_start = cx - radius; if (x_start < 0) x_start = 0;
    int y_start = cy - radius; if (y_start < 0) y_start = 0;
    int x_end   = cx + radius; if (x_end >= width)  x_end = width - 1;
    int y_end   = cy + radius; if (y_end >= height) y_end = height - 1;

    int bbox_w = x_end - x_start + 1;
    int bbox_h = y_end - y_start + 1;
    if (bbox_w <= 0 || bbox_h <= 0) return;

    uint32_t *temp_buffer = malloc(bbox_w * bbox_h * sizeof(uint32_t));
    if (!temp_buffer) return;

    int dx_move = cx - last_cx;
    int dy_move = cy - last_cy;

    for (int y = y_start; y <= y_end; y++)
    {
        for (int x = x_start; x <= x_end; x++)
        {
            int tx = x - x_start;
            int ty = y - y_start;

            uint32_t current_canvas_pixel = canvas[y * width + x];

            int dx = x - cx;
            int dy = y - cy;
            if (dx*dx + dy*dy > radius*radius) {
                temp_buffer[ty * bbox_w + tx] = current_canvas_pixel;
                continue;
            }

            int src_x = x - dx_move;
            int src_y = y - dy_move;

            if (src_x < 0 || src_x >= width || src_y < 0 || src_y >= height) {
                temp_buffer[ty * bbox_w + tx] = current_canvas_pixel;
                continue;
            }

            uint32_t src_pixel = canvas[src_y * width + src_x];

            uint8_t src_r = (src_pixel >> 24) & 0xFF;
            uint8_t src_g = (src_pixel >> 16) & 0xFF;
            uint8_t src_b = (src_pixel >>  8) & 0xFF;

            uint8_t dst_r = (current_canvas_pixel >> 24) & 0xFF;
            uint8_t dst_g = (current_canvas_pixel >> 16) & 0xFF;
            uint8_t dst_b = (current_canvas_pixel >>  8) & 0xFF;
            uint8_t alpha = current_canvas_pixel & 0xFF; // Keep destination alpha

            uint8_t final_r = (uint8_t)((src_r * strength) + (dst_r * (1.0f - strength)));
            uint8_t final_g = (uint8_t)((src_g * strength) + (dst_g * (1.0f - strength)));
            uint8_t final_b = (uint8_t)((src_b * strength) + (dst_b * (1.0f - strength)));

            temp_buffer[ty * bbox_w + tx] = (final_r << 24) | (final_g << 16) | (final_b << 8) | alpha;
        }
    }

    for (int y = y_start; y <= y_end; y++)
    {
        for (int x = x_start; x <= x_end; x++)
        {
            canvas[y * width + x] = temp_buffer[(y - y_start) * bbox_w + (x - x_start)];
        }
    }

    free(temp_buffer);
    dirty_mark_rect(x_start, y_start, x_end + 1, y_end + 1);
}

void apply_blend_tool(int cx, int cy, int radius, uint32_t *canvas, int width, int height, float strength)
{
    if (radius <= 0) return;

    int x_start = cx - radius; if (x_start < 0) x_start = 0;
    int y_start = cy - radius; if (y_start < 0) y_start = 0;
    int x_end   = cx + radius; if (x_end >= width)  x_end = width - 1;
    int y_end   = cy + radius; if (y_end >= height) y_end = height - 1;

    int bbox_w = x_end - x_start + 1;
    int bbox_h = y_end - y_start + 1;
    if (bbox_w <= 0 || bbox_h <= 0) return;

    uint32_t *temp_buffer = malloc(bbox_w * bbox_h * sizeof(uint32_t));
    if (!temp_buffer) return;

    for (int y = y_start; y <= y_end; y++)
    {
        for (int x = x_start; x <= x_end; x++)
        {
            int tx = x - x_start;
            int ty = y - y_start;

            uint32_t orig_pixel = canvas[y * width + x];

            int dx = x - cx;
            int dy = y - cy;
            if (dx*dx + dy*dy > radius*radius) {
                temp_buffer[ty * bbox_w + tx] = orig_pixel;
                continue;
            }

            uint32_t sum_r = 0, sum_g = 0, sum_b = 0, count = 0;

            for (int ky = -1; ky <= 1; ky++)
            {
                for (int kx = -1; kx <= 1; kx++)
                {
                    int px = x + kx;
                    int py = y + ky;

                    if (px >= 0 && px < width && py >= 0 && py < height)
                    {
                        uint32_t sample = canvas[py * width + px];
                        sum_r += (sample >> 24) & 0xFF;
                        sum_g += (sample >> 16) & 0xFF;
                        sum_b += (sample >>  8) & 0xFF;
                        count++;
                    }
                }
            }

            uint8_t avg_r = sum_r / count;
            uint8_t avg_g = sum_g / count;
            uint8_t avg_b = sum_b / count;

            uint8_t orig_r = (orig_pixel >> 24) & 0xFF;
            uint8_t orig_g = (orig_pixel >> 16) & 0xFF;
            uint8_t orig_b = (orig_pixel >>  8) & 0xFF;
            uint8_t alpha  = orig_pixel & 0xFF;

            uint8_t final_r = (uint8_t)((avg_r * strength) + (orig_r * (1.0f - strength)));
            uint8_t final_g = (uint8_t)((avg_g * strength) + (orig_g * (1.0f - strength)));
            uint8_t final_b = (uint8_t)((avg_b * strength) + (orig_b * (1.0f - strength)));

            temp_buffer[ty * bbox_w + tx] = (final_r << 24) | (final_g << 16) | (final_b << 8) | alpha;
        }
    }

    for (int y = y_start; y <= y_end; y++)
    {
        for (int x = x_start; x <= x_end; x++)
        {
            canvas[y * width + x] = temp_buffer[(y - y_start) * bbox_w + (x - x_start)];
        }
    }

    free(temp_buffer);
}

void apply_pencil(int mouse_x, int mouse_y, int radius, uint32_t brush_color, uint32_t* canvas, int canvas_width, int canvas_height) {
    uint32_t color = RGBA(R(brush_color), G(brush_color), B(brush_color), 40);
    apply_airbrush(mouse_x, mouse_y, radius, color, canvas, width, height, 12);
}
