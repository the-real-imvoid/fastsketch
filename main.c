// main.c
//
//  WRITTEN BY IMVOID
//

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_ttf.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>

#include "include/drawops.h"
#include "include/rgba.h"
#include "include/tool.h"
#include "include/undo.h"
#include "include/cursor.h"
#include "include/shapes.h"
#include "selection.h"

//------------------------ FASTSKETCH ------------------------------//
//                         ==========
// a very lightweight drawing program with some tools and a simple UI.
// The goal is to make it as fast as possible, while still being useful.



bool minimised = false;
int mouse_x, mouse_y;
int dragspot_x = 0;
int dragspot_y = 0;
bool drawing_shape = false;
bool prev_rect = false;
int rect_cur_x = 0;
int rect_cur_y = 0;
int line_cur_x = 0;
int line_cur_y = 0;
int ctrl_mod = 0;
int last_cx = 0;
int last_cy = 0;
float current_x_sel = 0;
float current_y_sel = 0;
float start_x_sel = 0;
float start_y_sel = 0;

int drag_offset_x = 0;
int drag_offset_y = 0;

bool sel_hole_punched = false;

#include "include/cui.hpp"

typedef enum {
    EDGE_NONE,
    EDGE_LEFT, EDGE_RIGHT, EDGE_TOP, EDGE_BOTTOM,
    EDGE_TOPLEFT, EDGE_TOPRIGHT, EDGE_BOTTOMLEFT, EDGE_BOTTOMRIGHT
} ResizeEdge;

typedef enum {
    STATE_SEL_IDLE, STATE_SEL_DRAWING, STATE_SEL_HAS_BOX, STATE_SEL_DRAGGING
} StateSel;

int current_state = STATE_SEL_IDLE;

static const int RESIZE_BORDER = 10;
static const int MIN_WIN_W = 400;
static const int MIN_WIN_H = 300;

static inline void enforce_square(int *x1, int *y1, int *x2, int *y2)
{
    int dx = *x2 - *x1;
    int dy = *y2 - *y1;

    int size = (abs(dx) > abs(dy)) ? abs(dx) : abs(dy);

    *x2 = *x1 + (dx < 0 ? -size : size);
    *y2 = *y1 + (dy < 0 ? -size : size);
}

static inline void screen_to_canvas_unclamped(
    int mx, int my,
    int camera_x, int camera_y,
    float zoom,
    float *out_x, float *out_y)
{
    *out_x = (mx - camera_x) / zoom;
    *out_y = (my - camera_y) / zoom;
}

static inline void canvas_to_screen(float cx, float cy,
                                    int camera_x, int camera_y,
                                    float zoom,
                                    int *sx, int *sy)
{
    *sx = (int)(camera_x + cx * zoom);
    *sy = (int)(camera_y + cy * zoom);
}

static inline void normalize_rect(int *x1, int *y1, int *x2, int *y2)
{
    if (*x1 > *x2) { int t = *x1; *x1 = *x2; *x2 = t; }
    if (*y1 > *y2) { int t = *y1; *y1 = *y2; *y2 = t; }
}

static void eyedropper(int cx, int cy, uint32_t *canvas, int width)
{
    set_color(canvas[cy * width + cx]);
}

static bool canvas_coords(int mx, int my, int camera_x, int camera_y, float zoom, int width, int height, float *out_x, float *out_y)
{
    float cx = (mx - camera_x) / zoom;
    float cy = (my - camera_y) / zoom;
    if (cx < 0 || cx >= width || cy < 0 || cy >= height)
        return false;
    *out_x = cx;
    *out_y = cy;
    return true;
}

static void painting_cancel(bool *painting, bool *has_last)
{
    if (*painting) {
        *painting = false;
        *has_last = false;
    }
}


static void commit_selection_if_active(uint32_t *canvas, int w, int h)
{
    if (current_state == STATE_SEL_IDLE || current_state == STATE_SEL_DRAWING)
        return;
    place_selection(sel_x, sel_y, canvas, w, h);
    free_selection();
    take_snapshot(canvas, w, h);
    sel_hole_punched = false;
    current_state = STATE_SEL_IDLE;
}

static void paste_from_clipboard(uint32_t *canvas, int w, int h,
                                 SDL_Renderer *renderer)
{
    if (!clipboard || clip_width <= 0 || clip_height <= 0) return;

    commit_selection_if_active(canvas, w, h);

    free_selection();

    size_t bytes = (size_t)clip_width * clip_height * sizeof(uint32_t);
    selection = malloc(bytes);
    if (!selection) return;
    memcpy(selection, clipboard, bytes);

    sel_width  = clip_width;
    sel_height = clip_height;
    sel_x      = (w - clip_width)  / 2;
    sel_y      = (h - clip_height) / 2;

    init_selection_tex_from_buffer(renderer);

    sel_hole_punched = false;
    current_state = STATE_SEL_HAS_BOX;
}

static ResizeEdge get_resize_edge(int x, int y, int w, int h, int border)
{
    bool left   = x < border;
    bool right  = x > w - border;
    bool top    = y < border;
    bool bottom = y > h - border;

    if (top && left)     return EDGE_TOPLEFT;
    if (top && right)    return EDGE_TOPRIGHT;
    if (bottom && left)  return EDGE_BOTTOMLEFT;
    if (bottom && right) return EDGE_BOTTOMRIGHT;
    if (left)             return EDGE_LEFT;
    if (right)            return EDGE_RIGHT;
    if (top)              return EDGE_TOP;
    if (bottom)           return EDGE_BOTTOM;
    return EDGE_NONE;
}

static SDL_Cursor *cursor_arrow    = NULL;
static SDL_Cursor *cursor_size_we  = NULL;
static SDL_Cursor *cursor_size_ns  = NULL;
static SDL_Cursor *cursor_size_nesw = NULL;
static SDL_Cursor *cursor_size_nwse = NULL;
static SDL_Cursor *current_cursor  = NULL;

static void init_cursors(void)
{
    cursor_arrow     = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    cursor_size_we   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    cursor_size_ns   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
    cursor_size_nesw = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
    cursor_size_nwse = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
    current_cursor   = cursor_arrow;
}

static void free_cursors(void)
{
    SDL_FreeCursor(cursor_arrow);
    SDL_FreeCursor(cursor_size_we);
    SDL_FreeCursor(cursor_size_ns);
    SDL_FreeCursor(cursor_size_nesw);
    SDL_FreeCursor(cursor_size_nwse);
}

static void set_cursor_for_edge(ResizeEdge edge)
{
    SDL_Cursor *c = cursor_arrow;
    switch (edge)
    {
        case EDGE_LEFT: case EDGE_RIGHT:          c = cursor_size_we;   break;
        case EDGE_TOP:  case EDGE_BOTTOM:         c = cursor_size_ns;   break;
        case EDGE_TOPLEFT: case EDGE_BOTTOMRIGHT: c = cursor_size_nwse; break;
        case EDGE_TOPRIGHT: case EDGE_BOTTOMLEFT: c = cursor_size_nesw; break;
        default: c = cursor_arrow; break;
    }
    if (c != current_cursor)
    {
        SDL_SetCursor(c);
        current_cursor = c;
    }
}

static void draw_canvas_border(SDL_Renderer *renderer, SDL_Rect view)
{
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 100, 100, 200, 255);

    for (int i = 0; i < 4; i++)
    {
        SDL_Rect r = {
            view.x + i,
            view.y + i,
            view.w - 2 * i,
            view.h - 2 * i
        };

        SDL_RenderDrawRect(renderer, &r);
    }
}

int main(int argc, char *argv[])
{
    int width  = 1920;
    int height = 1080;

    Uint32 *g_canvas = NULL;
    int brush_size = 6;

    int   camera_x = 500;
    int   camera_y = 500;
    float zoom     = 1.0f;

    const int TILE_SIZE = 16;

    bool  panning       = false;
    bool  painting      = false;
    float last_canvas_x = 0.0f;
    float last_canvas_y = 0.0f;
    bool  has_last      = false;

    ResizeEdge resize_edge        = EDGE_NONE;
    int resize_start_mouse_x = 0, resize_start_mouse_y = 0;
    int resize_start_win_x   = 0, resize_start_win_y   = 0;
    int resize_start_win_w   = 0, resize_start_win_h   = 0;

    g_canvas = malloc((size_t)width * height * sizeof(Uint32));
    if (!g_canvas) {
        printf("out of memory\n");
        return 1;
    }

    for (int i = 0; i < width * height; i++)
        g_canvas[i] = 0xFFFFFFFF;

    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
        printf("error initializing SDL: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *app = SDL_CreateWindow("FastSketch",
                                       SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED,
                                       1920, 1080,
                                       SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE);
    if (!app) {
        printf("error creating window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(app, MIN_WIN_W, MIN_WIN_H);
    SDL_SetWindowHitTest(app, hit_test, NULL);

    SDL_Renderer *renderer = SDL_CreateRenderer(app, -1, 0);
    if (!renderer) {
        printf("error creating renderer: %s\n", SDL_GetError());
        return 1;
    }

    drawops_set_size(width, height);

    SDL_Texture *texture = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             width, height);

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    Toolbar toolbar = { TOOL_BRUSH };

    undo_init();
    ui_init(app, renderer);
    init_cursors();
    rgba_init();
    cursor_init(renderer);

    take_snapshot(g_canvas, width, height);

    while (1)
    {
        uint32_t color = get_color();
        int density = get_density();
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            ui_process_event(&e);
            brush_size = get_brush_size();
            if (e.type == SDL_QUIT)
                goto quit;

            // ---- mouse button down ----
            if (e.type == SDL_MOUSEBUTTONDOWN)
            {
                int cur_win_w, cur_win_h;
                SDL_GetWindowSize(app, &cur_win_w, &cur_win_h);

                ResizeEdge edge = EDGE_NONE;
                if (e.button.button == SDL_BUTTON_LEFT &&
                    !ui_wants_mouse() &&
                    !(SDL_GetWindowFlags(app) & SDL_WINDOW_MAXIMIZED))
                {
                    edge = get_resize_edge(e.button.x, e.button.y, cur_win_w, cur_win_h, RESIZE_BORDER);
                }

                if (edge != EDGE_NONE)
                {
                    resize_edge = edge;
                    SDL_GetWindowPosition(app, &resize_start_win_x, &resize_start_win_y);
                    SDL_GetGlobalMouseState(&resize_start_mouse_x, &resize_start_mouse_y);
                    resize_start_win_w = cur_win_w;
                    resize_start_win_h = cur_win_h;
                }
                else if (e.button.button == SDL_BUTTON_RIGHT)
                {
                    painting_cancel(&painting, &has_last);
                    panning = true;
                }
                else if (e.button.button == SDL_BUTTON_LEFT && !ui_wants_mouse())
                {
                    int mx = e.button.x;
                    int my = e.button.y;

                    float canvas_x, canvas_y;
                    bool on_canvas = canvas_coords(mx, my, camera_x, camera_y, zoom,
                                                   width, height, &canvas_x, &canvas_y);

                    // --- Selection tool: handle state machine first ---
                    if (toolbar.selected == TOOL_SELECTION)
                    {
                        float sel_cx, sel_cy;
                        screen_to_canvas_unclamped(mx, my, camera_x, camera_y, zoom,
                                                   &sel_cx, &sel_cy);
                        int icx = (int)sel_cx;
                        int icy = (int)sel_cy;

                        if (current_state == STATE_SEL_HAS_BOX || current_state == STATE_SEL_DRAGGING)
                        {
                            bool inside = (icx >= sel_x && icx < sel_x + sel_width &&
                            icy >= sel_y && icy < sel_y + sel_height);


                            if (inside && current_state == STATE_SEL_HAS_BOX)
                            {
                                if (sel_hole_punched)
                                {
                                    yeet_old_area(g_canvas, width, height);
                                }

                                sel_hole_punched = true;
                                current_state = STATE_SEL_DRAGGING;
                                drag_offset_x = icx - sel_x;
                                drag_offset_y = icy - sel_y;
                            }

                            else if (!inside)
                            {
                                commit_selection_if_active(g_canvas, width, height);
                                current_state = STATE_SEL_DRAWING;
                                start_x_sel   = sel_cx;
                                start_y_sel   = sel_cy;
                                current_x_sel = sel_cx;
                                current_y_sel = sel_cy;
                            }
                        }
                        else
                        {
                            current_state = STATE_SEL_DRAWING;
                            start_x_sel   = sel_cx;
                            start_y_sel   = sel_cy;
                            current_x_sel = sel_cx;
                            current_y_sel = sel_cy;
                        }
                    }
                    else
                    {
                        commit_selection_if_active(g_canvas, width, height);

                        if (!on_canvas)
                            continue;

                        dragspot_x = (int)canvas_x;
                        dragspot_y = (int)canvas_y;

                        if (toolbar.selected == TOOL_BRUSH)
                        {
                            painting = true;
                            apply_brush_circle((int)canvas_x, (int)canvas_y, brush_size,
                                               color, g_canvas, width, height);
                            last_canvas_x = canvas_x;
                            last_canvas_y = canvas_y;
                            has_last = true;
                        }
                        else if (toolbar.selected == TOOL_ERASER)
                        {
                            painting = true;
                            apply_brush_circle((int)canvas_x, (int)canvas_y, brush_size,
                                               ERASER, g_canvas, width, height);
                            last_canvas_x = canvas_x;
                            last_canvas_y = canvas_y;
                            has_last = true;
                        }
                        else if (toolbar.selected == TOOL_AIRBRUSH)
                        {
                            painting = true;
                            apply_airbrush((int)canvas_x, (int)canvas_y, brush_size,
                                           color, g_canvas, width, height, density);
                            last_canvas_x = canvas_x;
                            last_canvas_y = canvas_y;
                            has_last = true;
                        }
                        else if (toolbar.selected == TOOL_BRIGHTNESS)
                        {
                            painting = true;
                            start_stroke(width, height);
                            apply_brightness_tool((int)canvas_x, (int)canvas_y, brush_size, g_canvas, width, height, ctrl_mod, 0.05);
                            last_canvas_x = canvas_x;
                            last_canvas_y = canvas_y;
                            has_last = true;
                        }
                        else if (toolbar.selected == TOOL_FILTER)
                        {
                            painting = true;
                            apply_filter_tool((int)canvas_x, (int)canvas_y, brush_size, g_canvas, width, height, ctrl_mod);
                            last_canvas_x = canvas_x;
                            last_canvas_y = canvas_y;
                            has_last = true;
                        }
                        else if (toolbar.selected == TOOL_SMUDGE)
                        {
                            painting = true;
                            apply_smudge_tool((int)canvas_x, (int)canvas_y, last_cx, last_cy, brush_size, g_canvas, width, height, (float)get_density()/20.0f);
                            last_canvas_x = canvas_x;
                            last_canvas_y = canvas_y;
                            has_last = true;
                        }
                        else if (toolbar.selected == TOOL_MIXER)
                        {
                            painting = true;
                            apply_blend_tool((int)canvas_x, (int)canvas_y, brush_size, g_canvas, width, height, 0.25f);
                            last_canvas_x = canvas_x;
                            last_canvas_y = canvas_y;
                            has_last = true;
                        }
                        else if (toolbar.selected == TOOL_PENCIL)
                        {
                            painting = true;
                            apply_pencil((int)canvas_x, (int)canvas_y, brush_size, color, g_canvas, width, height);
                            last_canvas_x = canvas_x;
                            last_canvas_y = canvas_y;
                            has_last = true;
                        }
                        else if (toolbar.selected == TOOL_FILL)
                        {
                            fill((int)canvas_x, (int)canvas_y, width, height, color, g_canvas);
                            take_snapshot(g_canvas, width, height);
                        }
                        else if (toolbar.selected == TOOL_EYEDROPPER)
                        {
                            eyedropper((int)canvas_x, (int)canvas_y, g_canvas, width);
                        }
                        else if (toolbar.selected == TOOL_RECT || toolbar.selected == TOOL_LINE)
                        {
                            rect_cur_x = (int)canvas_x;
                            rect_cur_y = (int)canvas_y;
                            line_cur_x = (int)canvas_x;
                            line_cur_y = (int)canvas_y;
                            drawing_shape = true;
                        }
                        else if (toolbar.selected == TOOL_CIRCLE)
                        {
                            float cx, cy;
                            screen_to_canvas_unclamped(e.button.x, e.button.y,
                                                       camera_x, camera_y, zoom,
                                                       &cx, &cy);
                            rect_cur_x = (int)cx;
                            rect_cur_y = (int)cy;
                            dragspot_x = (int)cx;
                            dragspot_y = (int)cy;
                            drawing_shape = true;
                        }
                    }
                }
            }

            else if (e.type == SDL_MOUSEBUTTONUP)
            {
                if (e.button.button == SDL_BUTTON_LEFT && resize_edge != EDGE_NONE)
                {
                    resize_edge = EDGE_NONE;
                }
                else if (e.button.button == SDL_BUTTON_RIGHT)
                {
                    panning = false;
                }
                else if (e.button.button == SDL_BUTTON_LEFT)
                {
                    if (painting && (toolbar.selected == TOOL_BRUSH ||
                        toolbar.selected == TOOL_ERASER ||
                        toolbar.selected == TOOL_AIRBRUSH ||
                        toolbar.selected == TOOL_BRIGHTNESS ||
                        toolbar.selected == TOOL_FILTER ||
                        toolbar.selected == TOOL_SMUDGE ||
                        toolbar.selected == TOOL_MIXER ||
                        toolbar.selected == TOOL_PENCIL))
                    {
                        take_snapshot(g_canvas, width, height);
                        painting = false;
                        has_last = false;
                    }

                    if (toolbar.selected == TOOL_RECT && drawing_shape)
                    {
                        int mx = e.button.x;
                        int my = e.button.y;
                        float canvas_x, canvas_y;
                        if (!canvas_coords(mx, my, camera_x, camera_y, zoom,
                            width, height, &canvas_x, &canvas_y))
                        {
                            canvas_x = (float)rect_cur_x;
                            canvas_y = (float)rect_cur_y;
                        }

                        int x1 = dragspot_x;
                        int y1 = dragspot_y;
                        int x2 = (int)canvas_x;
                        int y2 = (int)canvas_y;

                        SDL_Keymod mod = SDL_GetModState();
                        if (mod & KMOD_SHIFT)
                            enforce_square(&x1, &y1, &x2, &y2);

                        normalize_rect(&x1, &y1, &x2, &y2);

                        put_rect(g_canvas, x1, y1, x2, y2,
                                 brush_size, color, get_secondary_color(), width, height);
                        take_snapshot(g_canvas, width, height);
                        drawing_shape = false;
                        prev_rect = false;
                    }
                    else if (toolbar.selected == TOOL_LINE && drawing_shape)
                    {
                        int mx = e.button.x;
                        int my = e.button.y;
                        float canvas_x, canvas_y;
                        if (!canvas_coords(mx, my, camera_x, camera_y, zoom,
                            width, height, &canvas_x, &canvas_y))
                        {
                            canvas_x = (float)line_cur_x;
                            canvas_y = (float)line_cur_y;
                        }

                        int x1 = dragspot_x;
                        int y1 = dragspot_y;
                        int x2 = (int)canvas_x;
                        int y2 = (int)canvas_y;

                        bresenham(x1, y1, x2, y2, color, g_canvas, width, height, brush_size);
                        take_snapshot(g_canvas, width, height);
                        drawing_shape = false;
                        prev_rect = false;
                    }
                    else if (toolbar.selected == TOOL_CIRCLE && drawing_shape)
                    {
                        int x1 = dragspot_x;
                        int y1 = dragspot_y;
                        int x2 = rect_cur_x;
                        int y2 = rect_cur_y;

                        int cx = (x1 + x2) / 2;
                        int cy = (y1 + y2) / 2;

                        int rx = abs(x2 - x1) / 2;
                        int ry = abs(y2 - y1) / 2;

                        SDL_Keymod mod = SDL_GetModState();
                        if (mod & KMOD_SHIFT)
                        {
                            int r = (rx > ry) ? rx : ry;
                            rx = ry = r;
                        }

                        put_oval(g_canvas,
                                 cx, cy,
                                 rx, ry,
                                 get_secondary_color(),
                                 color,
                                 brush_size,
                                 width,
                                 height);

                        take_snapshot(g_canvas, width, height);
                        drawing_shape = false;
                    }
                    else if (toolbar.selected == TOOL_SELECTION)
                    {
                        if (current_state == STATE_SEL_DRAWING)
                        {
                            int x = (start_x_sel < current_x_sel) ? (int)start_x_sel : (int)current_x_sel;
                            int y = (start_y_sel < current_y_sel) ? (int)start_y_sel : (int)current_y_sel;
                            int w = (int)fabs(current_x_sel - start_x_sel);
                            int h = (int)fabs(current_y_sel - start_y_sel);

                            if (w > 0 && h > 0)
                            {
                                make_selection(x, y, x + w, y + h, g_canvas, width, renderer);
                                sel_hole_punched = true;
                                current_state = STATE_SEL_HAS_BOX;
                            }
                            else
                            {
                                current_state = STATE_SEL_IDLE;
                            }
                        }
                        else if (current_state == STATE_SEL_DRAGGING)
                        {
                            place_selection(sel_x, sel_y, g_canvas, width, height);
                            init_selection_tex_from_buffer(renderer);
                            sel_hole_punched = false;
                            current_state = STATE_SEL_HAS_BOX;
                        }
                    }
                }
            }

            else if (e.type == SDL_MOUSEMOTION)
            {
                mouse_x = e.motion.x;
                mouse_y = e.motion.y;

                if (resize_edge != EDGE_NONE)
                {
                    int gx, gy;
                    SDL_GetGlobalMouseState(&gx, &gy);
                    int dx = gx - resize_start_mouse_x;
                    int dy = gy - resize_start_mouse_y;

                    int new_x = resize_start_win_x;
                    int new_y = resize_start_win_y;
                    int new_w = resize_start_win_w;
                    int new_h = resize_start_win_h;

                    switch (resize_edge)
                    {
                        case EDGE_LEFT:
                            new_w = resize_start_win_w - dx;
                            new_x = resize_start_win_x + dx;
                            break;
                        case EDGE_RIGHT:
                            new_w = resize_start_win_w + dx;
                            break;
                        case EDGE_TOP:
                            new_h = resize_start_win_h - dy;
                            new_y = resize_start_win_y + dy;
                            break;
                        case EDGE_BOTTOM:
                            new_h = resize_start_win_h + dy;
                            break;
                        case EDGE_TOPLEFT:
                            new_w = resize_start_win_w - dx; new_x = resize_start_win_x + dx;
                            new_h = resize_start_win_h - dy; new_y = resize_start_win_y + dy;
                            break;
                        case EDGE_TOPRIGHT:
                            new_w = resize_start_win_w + dx;
                            new_h = resize_start_win_h - dy; new_y = resize_start_win_y + dy;
                            break;
                        case EDGE_BOTTOMLEFT:
                            new_w = resize_start_win_w - dx; new_x = resize_start_win_x + dx;
                            new_h = resize_start_win_h + dy;
                            break;
                        case EDGE_BOTTOMRIGHT:
                            new_w = resize_start_win_w + dx;
                            new_h = resize_start_win_h + dy;
                            break;
                        default: break;
                    }

                    if (new_w < MIN_WIN_W)
                    {
                        if (resize_edge == EDGE_LEFT || resize_edge == EDGE_TOPLEFT || resize_edge == EDGE_BOTTOMLEFT)
                            new_x = resize_start_win_x + (resize_start_win_w - MIN_WIN_W);
                        new_w = MIN_WIN_W;
                    }
                    if (new_h < MIN_WIN_H)
                    {
                        if (resize_edge == EDGE_TOP || resize_edge == EDGE_TOPLEFT || resize_edge == EDGE_TOPRIGHT)
                            new_y = resize_start_win_y + (resize_start_win_h - MIN_WIN_H);
                        new_h = MIN_WIN_H;
                    }

                    SDL_SetWindowSize(app, new_w, new_h);
                    SDL_SetWindowPosition(app, new_x, new_y);
                }
                else if (panning)
                {
                    camera_x += e.motion.xrel;
                    camera_y += e.motion.yrel;
                }
                else if (toolbar.selected == TOOL_SELECTION)
                {
                    float sel_cx, sel_cy;
                    screen_to_canvas_unclamped(e.motion.x, e.motion.y,
                                               camera_x, camera_y, zoom,
                                               &sel_cx, &sel_cy);

                    if (current_state == STATE_SEL_DRAWING)
                    {
                        current_x_sel = sel_cx;
                        current_y_sel = sel_cy;
                    }
                    else if (current_state == STATE_SEL_DRAGGING)
                    {
                        sel_x = (int)sel_cx - drag_offset_x;
                        sel_y = (int)sel_cy - drag_offset_y;
                    }
                }
                else if (painting && (toolbar.selected == TOOL_BRUSH ||
                    toolbar.selected == TOOL_ERASER ||
                    toolbar.selected == TOOL_AIRBRUSH ||
                    toolbar.selected == TOOL_BRIGHTNESS ||
                    toolbar.selected == TOOL_FILTER ||
                    toolbar.selected == TOOL_SMUDGE ||
                    toolbar.selected == TOOL_MIXER ||
                    toolbar.selected == TOOL_PENCIL))
                {
                    float canvas_x, canvas_y;
                    if (!canvas_coords(e.motion.x, e.motion.y,
                        camera_x, camera_y, zoom,
                        width, height, &canvas_x, &canvas_y))
                    {
                        has_last = false;
                        continue;
                    }

                    if (has_last)
                    {
                        float dx    = canvas_x - last_canvas_x;
                        float dy    = canvas_y - last_canvas_y;
                        int steps = (int)fmaxf(1.0f, fmaxf(fabsf(dx), fabsf(dy)));

                        for (int i = 1; i <= steps; i++)
                        {
                            float t = (float)i / (float)steps;
                            float x = last_canvas_x + dx * t;
                            float y = last_canvas_y + dy * t;
                            if (toolbar.selected == TOOL_BRUSH || toolbar.selected == TOOL_ERASER)
                                apply_brush_circle((int)x, (int)y, brush_size,
                                                   toolbar.selected == TOOL_BRUSH ? color : ERASER,
                                                   g_canvas, width, height);
                            else if (toolbar.selected == TOOL_AIRBRUSH)
                                apply_airbrush((int)x, (int)y, brush_size, color, g_canvas, width, height, density);
                            else if (toolbar.selected == TOOL_BRIGHTNESS)
                                apply_brightness_tool((int)x, (int)y, brush_size, g_canvas, width, height, ctrl_mod, 0.05);
                            else if (toolbar.selected == TOOL_FILTER)
                                apply_filter_tool((int)x, (int)y, brush_size, g_canvas, width, height, ctrl_mod);
                            else if (toolbar.selected == TOOL_SMUDGE)
                                apply_smudge_tool(canvas_x, canvas_y, last_cx, last_cy, brush_size, g_canvas, width, height, (float)get_density()/20.0f);
                            else if (toolbar.selected == TOOL_MIXER)
                                apply_blend_tool((int)x, (int)y, brush_size, g_canvas, width, height, 0.25f);
                            else if (toolbar.selected == TOOL_PENCIL)
                                apply_pencil((int)x, (int)y, brush_size, color, g_canvas, width, height);
                        }
                    }
                    last_canvas_x = canvas_x;
                    last_canvas_y = canvas_y;
                    has_last = true;
                }
                else if (toolbar.selected == TOOL_RECT && drawing_shape)
                {
                    float canvas_x, canvas_y;
                    if (canvas_coords(e.motion.x, e.motion.y,
                        camera_x, camera_y, zoom,
                        width, height, &canvas_x, &canvas_y))
                    {
                        rect_cur_x = (int)canvas_x;
                        rect_cur_y = (int)canvas_y;
                    }
                }
                else if (toolbar.selected == TOOL_LINE && drawing_shape)
                {
                    float canvas_x, canvas_y;
                    if (canvas_coords(e.motion.x, e.motion.y,
                        camera_x, camera_y, zoom,
                        width, height, &canvas_x, &canvas_y))
                    {
                        line_cur_x = (int)canvas_x;
                        line_cur_y = (int)canvas_y;
                    }
                }
                else if (toolbar.selected == TOOL_CIRCLE && drawing_shape)
                {
                    float cx, cy;
                    screen_to_canvas_unclamped(e.motion.x, e.motion.y,
                                               camera_x, camera_y, zoom,
                                               &cx, &cy);
                    rect_cur_x = (int)cx;
                    rect_cur_y = (int)cy;
                }
                else
                {
                    int cur_win_w, cur_win_h;
                    SDL_GetWindowSize(app, &cur_win_w, &cur_win_h);
                    if (!ui_wants_mouse() && !(SDL_GetWindowFlags(app) & SDL_WINDOW_MAXIMIZED))
                    {
                        ResizeEdge hover = get_resize_edge(e.motion.x, e.motion.y, cur_win_w, cur_win_h, RESIZE_BORDER);
                        set_cursor_for_edge(hover);
                    }
                    else
                    {
                        set_cursor_for_edge(EDGE_NONE);
                    }
                }

                float canvas_x, canvas_y;
                if (canvas_coords(e.motion.x, e.motion.y,
                    camera_x, camera_y, zoom,
                    width, height, &canvas_x, &canvas_y))
                {
                    last_cx = canvas_x;
                    last_cy = canvas_y;
                }
            }

            // ---- scroll wheel: zoom ----
            else if (e.type == SDL_MOUSEWHEEL)
            {
                int mx, my;
                SDL_GetMouseState(&mx, &my);

                float canvas_x = (mx - camera_x) / zoom;
                float canvas_y = (my - camera_y) / zoom;

                if (e.wheel.y > 0)
                    zoom *= 1.1f;
                else if (e.wheel.y < 0)
                    zoom /= 1.1f;

                camera_x = (int)(mx - canvas_x * zoom);
                camera_y = (int)(my - canvas_y * zoom);
            }

            else if (e.type == SDL_KEYDOWN)
            {
                SDL_Keymod mod = e.key.keysym.mod;
                if (mod & KMOD_CTRL)
                {
                    if (e.key.keysym.sym == SDLK_z)
                    {
                        undo(&g_canvas, &width, &height);
                        SDL_DestroyTexture(texture);
                        texture = SDL_CreateTexture(renderer,
                                                    SDL_PIXELFORMAT_RGBA8888,
                                                    SDL_TEXTUREACCESS_STREAMING,
                                                    width, height);
                        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
                        drawops_set_size(width, height);
                    }
                    else if (e.key.keysym.sym == SDLK_y)
                    {
                        redo(&g_canvas, &width, &height);
                        SDL_DestroyTexture(texture);
                        texture = SDL_CreateTexture(renderer,
                                                    SDL_PIXELFORMAT_RGBA8888,
                                                    SDL_TEXTUREACCESS_STREAMING,
                                                    width, height);
                        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
                        drawops_set_size(width, height);
                    }
                    else if (e.key.keysym.sym == SDLK_c)
                    {
                        if (current_state == STATE_SEL_HAS_BOX || current_state == STATE_SEL_DRAGGING)
                        {
                            copy();
                        }
                    }
                    else if (e.key.keysym.sym == SDLK_x)
                    {
                        take_snapshot(g_canvas, width, height);
                        if (current_state == STATE_SEL_HAS_BOX || current_state == STATE_SEL_DRAGGING)
                        {
                            copy();

                            if (current_state == STATE_SEL_DRAGGING)
                            {
                                free_selection();
                            }
                            else if (current_state == STATE_SEL_HAS_BOX)
                            {
                                yeet_old_area(g_canvas, width, height);
                                free_selection();
                            }

                            sel_hole_punched = false;
                            current_state = STATE_SEL_IDLE;
                        }
                    }
                    else if (e.key.keysym.sym == SDLK_v)
                    {
                        paste_from_clipboard(g_canvas, width, height, renderer);
                    }
                    if (e.key.keysym.sym == SDLK_LCTRL || e.key.keysym.sym == SDLK_RCTRL)
                    {
                        ctrl_mod = 1;
                    }
                }
            }
            else if (e.type == SDL_KEYUP)
            {
                if (e.key.keysym.sym == SDLK_LCTRL || e.key.keysym.sym == SDLK_RCTRL)
                {
                    ctrl_mod = 0;
                }
            }

            // ---- window events ----
            else if (e.type == SDL_WINDOWEVENT)
            {
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED)
                    minimised = true;
                else if (e.window.event == SDL_WINDOWEVENT_RESTORED ||
                    e.window.event == SDL_WINDOWEVENT_SHOWN)
                    minimised = false;
            }
        }

        int win_w, win_h;
        SDL_GetWindowSize(app, &win_w, &win_h);
        if (minimised || win_w <= 0 || win_h <= 0)
        {
            SDL_Delay(10);
            continue;
        }

        // ---- render ----
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);

        for (int y = 0; y < win_h; y += TILE_SIZE)
        {
            for (int x = 0; x < win_w; x += TILE_SIZE)
            {
                bool dark = ((x / TILE_SIZE) + (y / TILE_SIZE)) % 2;
                if (dark) SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
                else      SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);

                SDL_Rect r = { x, y, TILE_SIZE, TILE_SIZE };
                SDL_RenderFillRect(renderer, &r);
            }
        }

        SDL_Rect dst = {
            (int)camera_x,
            (int)camera_y,
            (int)(width  * zoom),
            (int)(height * zoom)
        };

        SDL_UpdateTexture(texture, NULL, g_canvas, width * sizeof(Uint32));
        SDL_RenderCopy(renderer, texture, NULL, &dst);

        // ---- shape previews ----
        if (drawing_shape && toolbar.selected == TOOL_RECT)
        {
            int x1 = dragspot_x;
            int y1 = dragspot_y;
            int x2 = rect_cur_x;
            int y2 = rect_cur_y;

            SDL_Keymod mod = SDL_GetModState();
            if (mod & KMOD_SHIFT)
                enforce_square(&x1, &y1, &x2, &y2);

            normalize_rect(&x1, &y1, &x2, &y2);

            int sx1, sy1, sx2, sy2;
            canvas_to_screen(x1, y1, camera_x, camera_y, zoom, &sx1, &sy1);
            canvas_to_screen(x2, y2, camera_x, camera_y, zoom, &sx2, &sy2);

            draw_rect_preview(renderer, sx1, sy1, sx2, sy2,
                              brush_size, color, get_secondary_color());
        }
        else if (drawing_shape && toolbar.selected == TOOL_LINE)
        {
            int sx1, sy1, sx2, sy2;
            canvas_to_screen(dragspot_x, dragspot_y, camera_x, camera_y, zoom, &sx1, &sy1);
            canvas_to_screen(line_cur_x, line_cur_y, camera_x, camera_y, zoom, &sx2, &sy2);
            preview_line(renderer, sx1, sy1, sx2, sy2, brush_size*zoom*2, color);
        }
        if (drawing_shape && toolbar.selected == TOOL_CIRCLE)
        {
            int x1 = dragspot_x;
            int y1 = dragspot_y;
            int x2 = rect_cur_x;
            int y2 = rect_cur_y;

            int cx = (x1 + x2) / 2;
            int cy = (y1 + y2) / 2;

            int rx = abs(x2 - x1) / 2;
            int ry = abs(y2 - y1) / 2;

            int sx_cx, sy_cy;
            canvas_to_screen(cx, cy, camera_x, camera_y, zoom, &sx_cx, &sy_cy);

            int sx_rx = (int)(rx * zoom);
            int sy_ry = (int)(ry * zoom);

            const uint8_t *keys = SDL_GetKeyboardState(NULL);
            if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT])
            {
                int r = (sx_rx > sy_ry) ? sx_rx : sy_ry;
                sx_rx = sy_ry = r;
            }

            preview_oval(renderer,
                         sx_cx, sy_cy,
                         sx_rx, sy_ry,
                         brush_size,
                         get_secondary_color(),
                         color);
        }
        prev_rect = false;

        if (toolbar.selected == TOOL_SELECTION)
        {
            if (current_state == STATE_SEL_DRAWING)
            {
                int x = (start_x_sel < current_x_sel) ? (int)start_x_sel : (int)current_x_sel;
                int y = (start_y_sel < current_y_sel) ? (int)start_y_sel : (int)current_y_sel;
                int w = (int)fabs(current_x_sel - start_x_sel);
                int h = (int)fabs(current_y_sel - start_y_sel);

                draw_marching_ants_specific(renderer, zoom, camera_x, camera_y, x, y, w, h);
            }
            else if (current_state == STATE_SEL_HAS_BOX)
            {
                draw_preview(renderer, sel_x, sel_y, zoom, camera_x, camera_y);

                draw_marching_ants_specific(renderer, zoom, camera_x, camera_y,
                                            sel_x, sel_y, sel_width, sel_height);
            }
            else if (current_state == STATE_SEL_DRAGGING)
            {
                draw_preview(renderer, sel_x, sel_y, zoom, camera_x, camera_y);
                draw_marching_ants_specific(renderer, zoom, camera_x, camera_y,
                                            sel_x, sel_y, sel_width, sel_height);
            }
        }

        draw_canvas_border(renderer, dst);
        if (current_state == STATE_SEL_HAS_BOX || current_state == STATE_SEL_DRAGGING)
            draw_marching_ants(renderer, zoom, camera_x, camera_y);
        step_ant_state();

        float cx = 0.0f;
        float cy = 0.0f;
        bool on_canvas = canvas_coords(mouse_x, mouse_y, camera_x, camera_y, zoom, width, height, &cx, &cy);
        if (on_canvas && (toolbar.selected == TOOL_BRUSH ||
            toolbar.selected == TOOL_ERASER ||
            toolbar.selected == TOOL_AIRBRUSH))
        {
            render_brush_cursor(mouse_x, mouse_y, brush_size, renderer, zoom, g_canvas, (int)cx, (int)cy, width, camera_x, camera_y);
        }
        else if (on_canvas && toolbar.selected == TOOL_FILL)
        {
            render_fill_cursor(mouse_x, mouse_y, renderer);
        }

        bool hide_cursor = !ui_wants_mouse() && on_canvas &&
        (toolbar.selected == TOOL_BRUSH ||
        toolbar.selected == TOOL_ERASER ||
        toolbar.selected == TOOL_FILL) && brush_size > 0;

        ui_begin_frame(&toolbar, app, win_w, win_h, &width, &height, &g_canvas, renderer, &texture);
        ui_end_frame();

        SDL_ShowCursor(hide_cursor ? SDL_DISABLE : SDL_ENABLE);

        SDL_RenderPresent(renderer);
    }

    quit:
    free(g_canvas);
    undo_cleanup();
    ui_shutdown();
    free_cursors();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(app);
    SDL_Quit();

    return 0;
}
